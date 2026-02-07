import { handleAIChat } from './ai-handler.js';
import {
  getAirConditioners,
  getAirConditioner,
  createAirConditioner,
  updateAirConditioner,
  deleteAirConditioner,
  controlAirConditioner
} from './ac-manager.js';

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    
    // Handle CORS preflight
    if (request.method === "OPTIONS") {
      return new Response(null, {
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Allow-Methods": "GET, POST, PUT, DELETE, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type"
        }
      });
    }
    
    // AI Chat endpoint
    if (url.pathname === "/api/chat" && request.method === "POST") {
      return handleAIChat(request, env);
    }
    
    // AC Management endpoints
    if (url.pathname === "/api/acs") {
      if (request.method === "GET") {
        const acs = await getAirConditioners(env);
        return jsonResponse(acs);
      }
      if (request.method === "POST") {
        const data = await request.json();
        const result = await createAirConditioner(data, env);
        return jsonResponse(result);
      }
    }
    
    // Single AC operations
    const acMatch = url.pathname.match(/^\/api\/acs\/([^\/]+)$/);
    if (acMatch) {
      const acId = acMatch[1];
      
      if (request.method === "GET") {
        const ac = await getAirConditioner(acId, env);
        return jsonResponse(ac || { error: 'AC not found' });
      }
      if (request.method === "PUT") {
        const updates = await request.json();
        const result = await updateAirConditioner(acId, updates, env);
        return jsonResponse(result);
      }
      if (request.method === "DELETE") {
        const result = await deleteAirConditioner(acId, env);
        return jsonResponse(result);
      }
    }
    
    // AC Control endpoint
    const controlMatch = url.pathname.match(/^\/api\/acs\/([^\/]+)\/control$/);
    if (controlMatch && request.method === "POST") {
      const acId = controlMatch[1];
      const command = await request.json();
      const result = await controlAirConditioner(acId, command, env);
      return jsonResponse(result);
    }
    
    // Config endpoint
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
      "cache-control": "no-store",
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, POST, PUT, DELETE, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type"
    }
  });
}
