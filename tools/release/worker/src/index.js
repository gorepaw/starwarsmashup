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

const KEY = /^(channels\/[a-z0-9_-]{1,32}\/[A-Za-z0-9._-]{1,64}|blobs\/[0-9a-f]{2}\/[0-9a-f]{64})$/;

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
  async fetch(request, env) {
    const url = new URL(request.url);
    const parts = url.pathname.split("/").filter(Boolean);
    const area = parts.shift();
    const key = parts.join("/");
    const bucket = area === "public" ? env.PUBLIC : area === "private" ? env.PRIVATE : null;
    if (!bucket) return text(404, "not found");
    const token = bearer(request);
    const publisher = tokenIs(token, env.PUBLISH_TOKEN);

    if (request.method === "GET" && key === "list") {
      if (!publisher) return text(401, "unauthorized");
      const listed = await bucket.list({ prefix: "blobs/", cursor: url.searchParams.get("cursor") || undefined, limit: 1000 });
      return Response.json({
        keys: listed.objects.map((o) => o.key),
        cursor: listed.truncated ? listed.cursor : null,
      });
    }
    if (!KEY.test(key)) return text(404, "not found");

    if (request.method === "PUT") {
      if (!publisher) return text(401, "unauthorized");
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
