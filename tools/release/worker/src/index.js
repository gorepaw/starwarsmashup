// The update server (docs/DISTRIBUTION.md): serves release manifests and
// content-addressed blobs from two R2 buckets, and takes uploads from
// tools/release/publish.py.
//
//   GET|HEAD /public/<key>     the player build's channel: anyone
//   GET|HEAD /private/<key>    the portable build's (it holds game assets): PORTABLE_TOKEN
//   PUT      /<area>/<key>     upload: PUBLISH_TOKEN
//   GET      /<area>/list?cursor=...   blob keys, 1000 at a time: PUBLISH_TOKEN
//
// <key> is channels/<channel>/<file> or blobs/<2 hex>/<64 hex>; nothing else.
//
// The game server's database backups (docs/BACKEND.md), in their own bucket:
//
//   PUT      /backups/<daily|weekly>/<file>    BACKUP_TOKEN (the server's); never overwrites
//   GET      /backups/<daily|weekly>/<file>    PUBLISH_TOKEN (the developer's)
//   GET      /backups/list?prefix=daily/       PUBLISH_TOKEN
//
// The server can add backups but can't read, replace or delete them, so a
// compromised server can't destroy its own history. The bucket's lifecycle
// rules expire them (daily/ after 30 days, weekly/ after 180).
//
// Usage guard (src/guard.js): every route that touches R2 first checks the
// guard's verdict; spent budgets, or no fresh verdict, mean 503.
//
//   GET      /guard                          the verdict and usage: PUBLISH_TOKEN

import { check, current } from "./guard.js";

const KEY = /^(channels\/[a-z0-9_-]{1,32}\/[A-Za-z0-9._-]{1,64}|blobs\/[0-9a-f]{2}\/[0-9a-f]{64})$/;
const BACKUP_KEY = /^(daily|weekly)\/[A-Za-z0-9._-]{1,96}$/;

async function backups(request, env, url, key) {
  const token = bearer(request);
  if (request.method === "PUT") {
    if (!tokenIs(token, env.BACKUP_TOKEN)) return text(401, "unauthorized");
    if (!BACKUP_KEY.test(key)) return text(404, "not found");
    const guard = await current(env);
    if (!guard.writes_open) return text(503, guard.reason);
    if (await env.BACKUPS.head(key)) return text(409, "exists");
    await env.BACKUPS.put(key, request.body, { httpMetadata: { contentType: "application/octet-stream" } });
    return text(201, "stored");
  }
  if (request.method !== "GET") return text(405, "method not allowed");
  if (!tokenIs(token, env.PUBLISH_TOKEN)) return text(401, "unauthorized");
  const guard = await current(env);
  if (!(key === "list" ? guard.writes_open : guard.reads_open)) return text(503, guard.reason);
  if (key === "list") {
    const prefix = url.searchParams.get("prefix") || "";
    const listed = await env.BACKUPS.list({ prefix, cursor: url.searchParams.get("cursor") || undefined, limit: 1000 });
    return Response.json({
      objects: listed.objects.map((o) => ({ key: o.key, size: o.size, uploaded: o.uploaded })),
      cursor: listed.truncated ? listed.cursor : null,
    });
  }
  if (!BACKUP_KEY.test(key)) return text(404, "not found");
  const object = await env.BACKUPS.get(key);
  if (!object) return text(404, "not found");
  return new Response(object.body, {
    headers: { "Content-Type": "application/octet-stream", "Content-Length": String(object.size), "Cache-Control": "private, no-store" },
  });
}

function bearer(request) {
  const h = request.headers.get("Authorization") || "";
  return h.startsWith("Bearer ") ? h.slice(7) : "";
}

// Constant-time comparison of a presented token with a secret.
function tokenIs(presented, secret) {
  if (!secret || !presented) return false;
  const enc = new TextEncoder();
  const a = enc.encode(presented);
  const b = enc.encode(secret);
  if (a.byteLength !== b.byteLength) {
    crypto.subtle.timingSafeEqual(b, b); // same work either way
    return false;
  }
  return crypto.subtle.timingSafeEqual(a, b);
}

function text(status, body) {
  return new Response(body + "\n", { status, headers: { "Content-Type": "text/plain" } });
}

export default {
  async scheduled(_event, env, ctx) {
    ctx.waitUntil(check(env));
  },

  async fetch(request, env) {
    const url = new URL(request.url);
    const parts = url.pathname.split("/").filter(Boolean);
    const area = parts.shift();
    const key = parts.join("/");
    if (area === "backups") return backups(request, env, url, key);
    if (area === "guard" && key === "" && request.method === "GET") {
      if (!tokenIs(bearer(request), env.PUBLISH_TOKEN)) return text(401, "unauthorized");
      return Response.json(await current(env));
    }
    const bucket = area === "public" ? env.PUBLIC : area === "private" ? env.PRIVATE : null;
    if (!bucket) return text(404, "not found");
    const token = bearer(request);
    const publisher = tokenIs(token, env.PUBLISH_TOKEN);

    if (request.method === "GET" && key === "list") {
      if (!publisher) return text(401, "unauthorized");
      const guard = await current(env);
      if (!guard.writes_open) return text(503, guard.reason); // listing is a Class A operation
      const listed = await bucket.list({ prefix: "blobs/", cursor: url.searchParams.get("cursor") || undefined, limit: 1000 });
      return Response.json({
        keys: listed.objects.map((o) => o.key),
        cursor: listed.truncated ? listed.cursor : null,
      });
    }
    if (!KEY.test(key)) return text(404, "not found");

    if (request.method === "PUT") {
      if (!publisher) return text(401, "unauthorized");
      const guard = await current(env);
      if (!guard.writes_open) return text(503, guard.reason);
      await bucket.put(key, request.body, {
        httpMetadata: { contentType: key.endsWith(".json") ? "application/json" : "application/octet-stream" },
      });
      return text(201, "stored");
    }

    if (request.method !== "GET" && request.method !== "HEAD") {
      return text(405, "method not allowed");
    }
    if (area === "private" && !publisher && !tokenIs(token, env.PORTABLE_TOKEN)) {
      return text(401, "unauthorized");
    }
    const guard = await current(env);
    if (!guard.reads_open) return text(503, guard.reason);
    const object = request.method === "HEAD" ? await bucket.head(key) : await bucket.get(key);
    if (!object) return text(404, "not found");
    const headers = new Headers();
    object.writeHttpMetadata(headers);
    headers.set("ETag", object.httpEtag);
    headers.set("Content-Length", String(object.size));
    // Blobs never change; manifests do.
    headers.set("Cache-Control", key.startsWith("blobs/") ? "public, max-age=31536000, immutable" : "no-cache");
    if (area === "private") headers.set("Cache-Control", "private, no-store");
    return new Response(request.method === "HEAD" ? null : object.body, { headers });
  },
};
