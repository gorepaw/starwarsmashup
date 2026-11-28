// node --test test/   (from tools/release/worker)
import assert from "node:assert/strict";
import { test } from "node:test";

import { BUDGETS, current, measure, verdict } from "../src/guard.js";

function analytics(ops, storage) {
  return async () => new Response(JSON.stringify({
    data: { viewer: { accounts: [{ ops, storage }] } },
  }), { status: 200 });
}

test("classifies operations and sums each bucket's newest storage", async () => {
  globalThis.fetch = analytics(
    [
      { sum: { requests: 100 }, dimensions: { actionType: "PutObject" } },
      { sum: { requests: 5 }, dimensions: { actionType: "ListObjects" } },
      { sum: { requests: 7 }, dimensions: { actionType: "SomethingNew" } }, // unknown: Class A
      { sum: { requests: 1000 }, dimensions: { actionType: "GetObject" } },
      { sum: { requests: 10 }, dimensions: { actionType: "HeadObject" } },
      { sum: { requests: 50 }, dimensions: { actionType: "DeleteObject" } }, // free
    ],
    [
      { max: { payloadSize: 300, metadataSize: 10 }, dimensions: { bucketName: "a", datetime: "2026-11-28T10:00:00Z" } },
      { max: { payloadSize: 200, metadataSize: 10 }, dimensions: { bucketName: "b", datetime: "2026-11-28T10:00:00Z" } },
      { max: { payloadSize: 999, metadataSize: 0 }, dimensions: { bucketName: "a", datetime: "2026-11-28T09:00:00Z" } },
    ],
  );
  const usage = await measure({ CF_ACCOUNT_ID: "x", CF_ANALYTICS_TOKEN: "t" });
  assert.equal(usage.class_a, 112);
  assert.equal(usage.class_b, 1010);
  assert.equal(usage.storage_bytes, 520);
});

test("analytics errors throw, so no verdict is stored", async () => {
  globalThis.fetch = async () => new Response(JSON.stringify({ errors: [{ message: "denied" }] }), { status: 200 });
  await assert.rejects(measure({ CF_ACCOUNT_ID: "x", CF_ANALYTICS_TOKEN: "t" }), /denied/);
});

test("budgets close writes and reads at their limits", () => {
  const under = { class_a: BUDGETS.class_a - 1, class_b: BUDGETS.class_b - 1, storage_bytes: BUDGETS.storage_bytes - 1 };
  assert.deepEqual([verdict(under, 0).writes_open, verdict(under, 0).reads_open], [true, true]);
  assert.equal(verdict({ ...under, class_a: BUDGETS.class_a }, 0).writes_open, false);
  assert.equal(verdict({ ...under, storage_bytes: BUDGETS.storage_bytes }, 0).writes_open, false);
  const readsSpent = verdict({ ...under, class_b: BUDGETS.class_b }, 0);
  assert.deepEqual([readsSpent.writes_open, readsSpent.reads_open], [true, false]);
});

function bucketHolding(state) {
  return { get: async () => (state ? { json: async () => state } : null) };
}

test("no verdict, or a stale one, closes everything", async () => {
  const now = 1_000_000_000_000;
  let g = await current({ PRIVATE: bucketHolding(null) }, now);
  assert.deepEqual([g.reads_open, g.writes_open], [false, false]);
  const fresh = verdict({ class_a: 0, class_b: 0, storage_bytes: 0 }, now);
  // A new isolate's memo: far enough ahead in time to re-read.
  g = await current({ PRIVATE: bucketHolding(fresh) }, now + 10 * 60 * 1000);
  assert.deepEqual([g.reads_open, g.writes_open], [true, true]);
  g = await current({ PRIVATE: bucketHolding(fresh) }, now + 3 * 3600 * 1000);
  assert.deepEqual([g.reads_open, g.writes_open], [false, false]);
  assert.match(g.reason, /stale/);
});
