export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/config.json") {
      return jsonResponse({
        firebase: {
          url: env.FIREBASE_DB_URL || "",
          auth: env.FIREBASE_AUTH || "",
          deviceId: env.FIREBASE_DEVICE_ID || "",
          statusPath: env.FIREBASE_STATUS_PATH || "",
          historyPath: env.FIREBASE_HISTORY_PATH || "",
          alarmsPath: env.FIREBASE_ALARMS_PATH || "",
          historyOrderBy: env.FIREBASE_HISTORY_ORDER_BY || ""
        }
      });
    }

    if (env.ASSETS) {
      return env.ASSETS.fetch(request);
    }

    return new Response("Not Found", { status: 404 });
  }
};

function jsonResponse(payload) {
  return new Response(JSON.stringify(payload, null, 2), {
    status: 200,
    headers: {
      "content-type": "application/json",
      "cache-control": "no-store"
    }
  });
}
