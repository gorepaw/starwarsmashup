// Usage guard: hard budgets so this Worker can never run up R2 charges
// (docs/BACKEND.md, "Costs"). The project's rule: an outage is acceptable,
// an excess charge is not.
//
// Workers requests and CPU are capped by Cloudflare's Spend Management (a $0.01
// limit that blocks usage); it doesn't cover R2. So every 10 minutes the
// scheduled handler asks the GraphQL Analytics API for the whole account's R2
// usage over the last 31 days (at least any billing month; the free allowance
// is account-wide, so other projects' buckets count too) and stores a verdict.
// The fetch handler refuses what a spent budget covers, and refuses everything
// if the verdict is missing or older than STALE_MS: no data, no service.
//
// Secrets: CF_ACCOUNT_ID, CF_ANALYTICS_TOKEN (an API token with Account
// Analytics: Read only).

// Hard budgets, below R2's free monthly allowance (10 GB-month storage,
// 1M Class A operations, 10M Class B operations).
export const BUDGETS = {
  storage_bytes: 8e9,
  class_a: 800_000,
  class_b: 8_000_000,
};
const STALE_MS = 2 * 3600 * 1000;
const WINDOW_MS = 31 * 24 * 3600 * 1000;
const STATE_KEY = "guard/state.json"; // in the PRIVATE bucket; not servable (not a channel or blob key)
const MEMO_MS = 5 * 60 * 1000;        // a verdict is re-read at most this often per isolate

// R2's operation classes (https://developers.cloudflare.com/r2/pricing/).
// Anything not named here is counted as Class A, the dearer one.
const CLASS_B = new Set(["HeadBucket", "HeadObject", "GetObject", "UsageSummary", "GetBucketEncryption",
  "GetBucketLocation", "GetBucketCors", "GetBucketLifecycleConfiguration"]);
const FREE = new Set(["DeleteObject", "DeleteBucket", "AbortMultipartUpload"]);

const QUERY = `query ($account: string!, $start: Time!, $end: Time!, $storageStart: Time!) {
  viewer { accounts(filter: { accountTag: $account }) {
    ops: r2OperationsAdaptiveGroups(limit: 10000, filter: { datetime_geq: $start, datetime_leq: $end }) {
      sum { requests }
      dimensions { actionType }
    }
    storage: r2StorageAdaptiveGroups(limit: 10000, filter: { datetime_geq: $storageStart, datetime_leq: $end },
                                     orderBy: [datetime_DESC]) {
      max { payloadSize metadataSize }
      dimensions { bucketName datetime }
    }
  } }
}`;

// Month-to-date-ish R2 usage for the account, from the GraphQL Analytics API.
export async function measure(env, now = Date.now()) {
  const end = new Date(now).toISOString();
  const response = await fetch("https://api.cloudflare.com/client/v4/graphql", {
    method: "POST",
    headers: { Authorization: `Bearer ${env.CF_ANALYTICS_TOKEN}`, "Content-Type": "application/json" },
    body: JSON.stringify({
      query: QUERY,
      variables: {
        account: env.CF_ACCOUNT_ID,
        start: new Date(now - WINDOW_MS).toISOString(),
        storageStart: new Date(now - 2 * 24 * 3600 * 1000).toISOString(),
        end,
      },
    }),
  });
  const body = await response.json();
  if (!response.ok || body.errors?.length) {
    throw new Error(`analytics: HTTP ${response.status} ${JSON.stringify(body.errors || body).slice(0, 300)}`);
  }
  const account = body.data?.viewer?.accounts?.[0];
  if (!account) throw new Error("analytics: no such account");
  let class_a = 0, class_b = 0;
  for (const group of account.ops) {
    const action = group.dimensions.actionType;
    if (FREE.has(action)) continue;
    if (CLASS_B.has(action)) class_b += group.sum.requests;
    else class_a += group.sum.requests;
  }
  // Each bucket's newest storage sample (rows come newest first).
  const latest = new Map();
  for (const group of account.storage) {
    const bucket = group.dimensions.bucketName;
    if (!latest.has(bucket)) latest.set(bucket, group.max.payloadSize + group.max.metadataSize);
  }
  let storage_bytes = 0;
  for (const bytes of latest.values()) storage_bytes += bytes;
  return { class_a, class_b, storage_bytes, buckets: Object.fromEntries(latest) };
}

export function verdict(usage, checked) {
  return {
    checked,
    usage,
    budgets: BUDGETS,
    writes_open: usage.class_a < BUDGETS.class_a && usage.storage_bytes < BUDGETS.storage_bytes,
    reads_open: usage.class_b < BUDGETS.class_b,
  };
}

// The scheduled handler: measure and store the verdict. On failure, nothing
// is stored, so the old verdict goes stale and the service closes.
export async function check(env) {
  const usage = await measure(env);
  const state = verdict(usage, Date.now());
  await env.PRIVATE.put(STATE_KEY, JSON.stringify(state), { httpMetadata: { contentType: "application/json" } });
  return state;
}

let memo = null; // { at, state }

// The current verdict, or a closed one if there's none or it's stale.
export async function current(env, now = Date.now()) {
  if (!memo || now - memo.at > MEMO_MS) {
    const object = await env.PRIVATE.get(STATE_KEY);
    memo = { at: now, state: object ? await object.json() : null };
  }
  const state = memo.state;
  if (!state || now - state.checked > STALE_MS) {
    return { reads_open: false, writes_open: false, reason: state ? "usage guard data is stale" : "usage guard has no data" };
  }
  return {
    ...state,
    reason: state.reads_open && state.writes_open ? "" : "a usage budget is spent until the next billing month",
  };
}
