#include "quickjs.h"
#include "web.h"
#include <stdlib.h>
#include <string.h>
static JSRuntime *runtime;
static JSContext *context;
static JSClassID node_class;
static JSValue wrappers[WEB_NODES];
static u64 deadline, origin;
static int interrupted;
static int document_state;
struct Handler {
  int node;
  char type[24];
  JSValue function;
};
static struct Handler handlers[128];
static unsigned handler_count;
struct Timer {
  unsigned id;
  u64 due, interval;
  JSValue function;
};
static struct Timer timers[32];
static unsigned timer_id;
struct Fetch {
  int used;
  JSValue resolve, reject;
};
static struct Fetch fetches[48];
static int interrupt(JSRuntime *rt, void *opaque) {
  (void)rt;
  (void)opaque;
  if (cycles() > deadline) {
    interrupted = 1;
    return 1;
  }
  return 0;
}
static void budget(void) {
  deadline = cycles() + clock_hz / 5;
  interrupted = 0;
}
static void report(JSValue result) {
  if (JS_IsException(result)) {
    JSValue error = JS_GetException(context);
    const char *s = JS_ToCString(context, error);
    serial("JS ERROR: ");
    serial(s ? s : "exception");
    serial("\n");
    JS_FreeCString(context, s);
    JS_FreeValue(context, error);
    gui_status(
        interrupted
            ? "JavaScript detenido: limite de tiempo"
            : "Pagina con errores JavaScript; APIs de navegador limitadas");
  }
  JS_FreeValue(context, result);
}
static int node_id(JSValueConst value) {
  intptr_t id = (intptr_t)JS_GetOpaque(value, node_class) - 1;
  return id >= 0 && id < web_node_count ? (int)id : -1;
}
static JSValue node_value(int id) {
  if (id < 0 || id >= web_node_count)
    return JS_NULL;
  if (JS_IsUndefined(wrappers[id])) {
    wrappers[id] = JS_NewObjectClass(context, node_class);
    JS_SetOpaque(wrappers[id], (void *)(intptr_t)(id + 1));
  }
  return JS_DupValue(context, wrappers[id]);
}
static JSValue promise(JSValue value, int reject) {
  JSValue callbacks[2];
  JSValue p = JS_NewPromiseCapability(context, callbacks);
  JSValue result = JS_Call(context, callbacks[reject], JS_UNDEFINED, 1, &value);
  JS_FreeValue(context, result);
  JS_FreeValue(context, value);
  JS_FreeValue(context, callbacks[0]);
  JS_FreeValue(context, callbacks[1]);
  return p;
}
static JSValue method_find(JSContext *ctx, JSValueConst self, int argc,
                           JSValueConst *argv, int magic) {
  (void)self;
  if (!argc)
    return JS_NULL;
  const char *s = JS_ToCString(ctx, argv[0]);
  if (!s)
    return JS_EXCEPTION;
  int node = magic ? web_query(s) : web_find(s);
  JS_FreeCString(ctx, s);
  return node_value(node);
}
static JSValue method_create(JSContext *ctx, JSValueConst self, int argc,
                             JSValueConst *argv, int magic) {
  (void)self;
  const char *s = argc ? JS_ToCString(ctx, argv[0]) : 0;
  if (!s)
    return JS_NULL;
  int node = web_create(magic ? "#text" : s);
  if (magic && node >= 0)
    web_set_text(node, s);
  JS_FreeCString(ctx, s);
  return node_value(node);
}
static JSValue method_append(JSContext *ctx, JSValueConst self, int argc,
                             JSValueConst *argv) {
  (void)ctx;
  int a = node_id(self), b = argc ? node_id(argv[0]) : -1;
  if (a >= 0 && b >= 0)
    web_append(a, b);
  return node_value(b);
}
static JSValue method_attr(JSContext *ctx, JSValueConst self, int argc,
                           JSValueConst *argv, int magic) {
  int id = node_id(self);
  if (id < 0 || !argc)
    return JS_NULL;
  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;
  if (magic) {
    const char *v = argc > 1 ? JS_ToCString(ctx, argv[1]) : 0;
    if (v) {
      web_set_attr(id, name, v);
      JS_FreeCString(ctx, v);
    }
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
  }
  JSValue result = JS_NewString(ctx, web_get_attr(id, name));
  JS_FreeCString(ctx, name);
  return result;
}
static void register_handler(int node, const char *type, JSValueConst fn) {
  if (!JS_IsFunction(context, fn) || handler_count == 128)
    return;
  struct Handler *h = &handlers[handler_count++];
  h->node = node;
  strcopy(h->type, type, sizeof(h->type));
  h->function = JS_DupValue(context, fn);
}
static JSValue method_listen(JSContext *ctx, JSValueConst self, int argc,
                             JSValueConst *argv) {
  if (argc < 2)
    return JS_UNDEFINED;
  const char *s = JS_ToCString(ctx, argv[0]);
  if (s) {
    int id = node_id(self);
    register_handler(id < 0 ? 0 : id, s, argv[1]);
    JS_FreeCString(ctx, s);
  }
  return JS_UNDEFINED;
}
static JSValue method_query_all(JSContext *ctx, JSValueConst self, int argc,
                                JSValueConst *argv) {
  (void)self;
  JSValue out = JS_NewArray(ctx);
  if (!argc)
    return out;
  const char *tag = JS_ToCString(ctx, argv[0]);
  if (!tag)
    return out;
  unsigned at = 0;
  for (int i = 1; i < web_node_count; i++)
    if (!strcmp(tag, "*") || !strcmp(tag, web_nodes[i].tag))
      JS_SetPropertyUint32(ctx, out, at++, node_value(i));
  JS_FreeCString(ctx, tag);
  return out;
}
static const char *attribute_names[] = {
    "textContent", "innerText", "innerHTML", "id",   "className", "value",
    "href",        "src",       "type",      "name", "onclick"};
static JSValue node_get(JSContext *ctx, JSValueConst self, int magic) {
  int id = node_id(self);
  if (id < 0)
    return JS_UNDEFINED;
  if (magic < 3)
    return JS_NewString(ctx, web_get_text(id));
  if (magic == 10)
    return JS_UNDEFINED;
  if (magic >= 11) {
    struct WebNode *n = &web_nodes[id];
    if (magic == 11)
      return node_value(n->parent);
    if (magic == 12)
      return node_value(n->first);
    if (magic == 13)
      return node_value(n->last);
    if (magic == 14)
      return JS_NewInt32(ctx, !strcmp(n->tag, "#text") ? 3 : 1);
    if (magic == 15) {
      char text[16];
      strcopy(text, n->tag, sizeof(text));
      for (char *p = text; *p; p++)
        if (*p >= 'a' && *p <= 'z')
          *p -= 32;
      return JS_NewString(ctx, text);
    }
    JSValue list = JS_NewArray(ctx);
    unsigned at = 0;
    for (int c = n->first; c >= 0; c = web_nodes[c].next)
      if (magic == 16 || strcmp(web_nodes[c].tag, "#text"))
        JS_SetPropertyUint32(ctx, list, at++, node_value(c));
    return list;
  }
  return JS_NewString(ctx, web_get_attr(id, attribute_names[magic]));
}
static JSValue node_set(JSContext *ctx, JSValueConst self, JSValueConst value,
                        int magic) {
  int id = node_id(self);
  if (id < 0)
    return JS_UNDEFINED;
  if (magic == 10) {
    register_handler(id, "click", value);
    return JS_UNDEFINED;
  }
  const char *s = JS_ToCString(ctx, value);
  if (!s)
    return JS_EXCEPTION;
  if (magic == 2)
    web_set_html(id, s);
  else if (magic < 2)
    web_set_text(id, s);
  else
    web_set_attr(id, attribute_names[magic], s);
  JS_FreeCString(ctx, s);
  return JS_UNDEFINED;
}
static JSValue document_get(JSContext *ctx, JSValueConst self, int magic) {
  (void)self;
  if (magic == 0)
    return node_value(web_body());
  if (magic == 1) {
    int root = web_query("html");
    return node_value(root < 0 ? web_body() : root);
  }
  if (magic == 2)
    return JS_NewString(ctx, document_state == 2 ? "complete" : document_state == 1 ? "interactive" : "loading");
  if (magic == 3)
    return JS_NewString(ctx, web_title);
  return JS_NewString(ctx, web_location());
}
static JSValue document_set(JSContext *ctx, JSValueConst self,
                            JSValueConst value, int magic) {
  (void)self;
  const char *s = JS_ToCString(ctx, value);
  if (!s)
    return JS_EXCEPTION;
  if (magic == 3) {
    strcopy(web_title, s, sizeof(web_title));
    web_dirty = 1;
  }
  JS_FreeCString(ctx, s);
  return JS_UNDEFINED;
}
static JSValue location_get(JSContext *ctx, JSValueConst self) {
  (void)self;
  return JS_NewString(ctx, web_location());
}
static JSValue location_set(JSContext *ctx, JSValueConst self,
                            JSValueConst value) {
  (void)self;
  const char *s = JS_ToCString(ctx, value);
  if (!s)
    return JS_EXCEPTION;
  web_queue_navigation(s);
  JS_FreeCString(ctx, s);
  return JS_UNDEFINED;
}
static JSValue location_assign(JSContext *ctx, JSValueConst self, int argc,
                               JSValueConst *argv) {
  return argc ? location_set(ctx, self, argv[0]) : JS_UNDEFINED;
}
static JSValue console_log(JSContext *ctx, JSValueConst self, int argc,
                           JSValueConst *argv) {
  (void)self;
  serial("JS LOG: ");
  for (int i = 0; i < argc && i < 16; i++) {
    const char *s = JS_ToCString(ctx, argv[i]);
    if (s) {
      serial(s);
      JS_FreeCString(ctx, s);
    }
    if (i + 1 < argc)
      serial(" ");
  }
  serial("\n");
  return JS_UNDEFINED;
}
static JSValue timer_set(JSContext *ctx, JSValueConst self, int argc,
                         JSValueConst *argv, int repeat) {
  (void)self;
  if (!argc || !JS_IsFunction(ctx, argv[0]))
    return JS_NewInt32(ctx, 0);
  int delay = 0;
  if (argc > 1)
    JS_ToInt32(ctx, &delay, argv[1]);
  if (delay < 1)
    delay = 1;
  if (delay > 60000)
    delay = 60000;
  for (unsigned i = 0; i < 32; i++)
    if (!timers[i].id) {
      struct Timer *t = &timers[i];
      t->id = ++timer_id;
      t->due = cycles() + clock_hz * delay / 1000;
      t->interval = repeat ? clock_hz * delay / 1000 : 0;
      t->function = JS_DupValue(ctx, argv[0]);
      return JS_NewUint32(ctx, t->id);
    }
  return JS_NewInt32(ctx, 0);
}
static JSValue timer_clear(JSContext *ctx, JSValueConst self, int argc,
                           JSValueConst *argv) {
  (void)self;
  u32 id = 0;
  if (argc)
    JS_ToUint32(ctx, &id, argv[0]);
  for (unsigned i = 0; i < 32; i++)
    if (timers[i].id == id) {
      JS_FreeValue(ctx, timers[i].function);
      timers[i].id = 0;
    }
  return JS_UNDEFINED;
}
static JSValue performance_now(JSContext *ctx, JSValueConst self, int argc,
                               JSValueConst *argv) {
  (void)self;
  (void)argc;
  (void)argv;
  return JS_NewFloat64(ctx, (double)(cycles() - origin) * 1000.0 / clock_hz);
}
static JSValue response_read(JSContext *ctx, JSValueConst self, int argc,
                             JSValueConst *argv, int json) {
  (void)argc;
  (void)argv;
  JSValue body = JS_GetPropertyStr(ctx, self, "_body");
  if (!json)
    return promise(body, 0);
  size_t n;
  const char *s = JS_ToCStringLen(ctx, &n, body);
  JSValue result = s ? JS_ParseJSON(ctx, s, n, "fetch") : JS_EXCEPTION;
  JS_FreeCString(ctx, s);
  JS_FreeValue(ctx, body);
  if (JS_IsException(result)) {
    JSValue error = JS_GetException(ctx);
    return promise(error, 1);
  }
  return promise(result, 0);
}
static JSValue fetch_start(JSContext *ctx, JSValueConst self, int argc,
                           JSValueConst *argv) {
  (void)self;
  if (!argc)
    return promise(JS_NewString(ctx, "fetch URL required"), 1);
  if (argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
    JSValue method = JS_GetPropertyStr(ctx, argv[1], "method");
    const char *name = JS_IsUndefined(method) ? 0 : JS_ToCString(ctx, method);
    int supported = !name || !strcmp(name, "GET");
    JS_FreeCString(ctx, name);
    JS_FreeValue(ctx, method);
    if (!supported)
      return promise(JS_NewString(ctx, "Only same-origin GET fetch is supported"), 1);
  }
  const char *s = JS_ToCString(ctx, argv[0]);
  if (!s)
    return JS_EXCEPTION;
  int slot = -1;
  for (unsigned i = 0; i < 48; i++)
    if (!fetches[i].used) {
      slot = i;
      break;
    }
  JSValue callbacks[2], p = JS_NewPromiseCapability(ctx, callbacks);
  if (slot < 0 || !web_fetch(s, slot)) {
    JSValue error = JS_NewString(ctx, "fetch queue full");
    JSValue ret = JS_Call(ctx, callbacks[1], JS_UNDEFINED, 1, &error);
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, error);
    JS_FreeValue(ctx, callbacks[0]);
    JS_FreeValue(ctx, callbacks[1]);
  } else {
    fetches[slot].used = 1;
    fetches[slot].resolve = callbacks[0];
    fetches[slot].reject = callbacks[1];
  }
  JS_FreeCString(ctx, s);
  return p;
}
void web_js_fetch_result(int token, const char *body, int code) {
  if (!context || token < 0 || token >= 48 || !fetches[token].used)
    return;
  struct Fetch *f = &fetches[token];
  budget();
  JSValue result;
  if (!code)
    result = JS_NewString(context, "Network request failed");
  else {
    result = JS_NewObject(context);
    JS_SetPropertyStr(context, result, "status", JS_NewInt32(context, code));
    JS_SetPropertyStr(context, result, "ok",
                      JS_NewBool(context, code >= 200 && code < 300));
    JS_SetPropertyStr(context, result, "_body", JS_NewString(context, body));
    JS_SetPropertyStr(context, result, "text",
                      JS_NewCFunctionMagic(context, response_read, "text", 0,
                                           JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(context, result, "json",
                      JS_NewCFunctionMagic(context, response_read, "json", 0,
                                           JS_CFUNC_generic_magic, 1));
  }
  report(JS_Call(context, code ? f->resolve : f->reject, JS_UNDEFINED, 1,
                 &result));
  JS_FreeValue(context, result);
  JS_FreeValue(context, f->resolve);
  JS_FreeValue(context, f->reject);
  f->used = 0;
}
static const JSCFunctionListEntry node_functions[] = {
    JS_CFUNC_DEF("appendChild", 1, method_append),
    JS_CFUNC_DEF("append", 1, method_append),
    JS_CFUNC_DEF("addEventListener", 2, method_listen),
    JS_CFUNC_MAGIC_DEF("getAttribute", 1, method_attr, 0),
    JS_CFUNC_MAGIC_DEF("setAttribute", 2, method_attr, 1),
    JS_CFUNC_MAGIC_DEF("querySelector", 1, method_find, 1),
    JS_CGETSET_MAGIC_DEF("textContent", node_get, node_set, 0),
    JS_CGETSET_MAGIC_DEF("innerText", node_get, node_set, 1),
    JS_CGETSET_MAGIC_DEF("innerHTML", node_get, node_set, 2),
    JS_CGETSET_MAGIC_DEF("id", node_get, node_set, 3),
    JS_CGETSET_MAGIC_DEF("className", node_get, node_set, 4),
    JS_CGETSET_MAGIC_DEF("value", node_get, node_set, 5),
    JS_CGETSET_MAGIC_DEF("href", node_get, node_set, 6),
    JS_CGETSET_MAGIC_DEF("src", node_get, node_set, 7),
    JS_CGETSET_MAGIC_DEF("type", node_get, node_set, 8),
    JS_CGETSET_MAGIC_DEF("name", node_get, node_set, 9),
    JS_CGETSET_MAGIC_DEF("onclick", node_get, node_set, 10),
    JS_CGETSET_MAGIC_DEF("parentNode", node_get, 0, 11),
    JS_CGETSET_MAGIC_DEF("firstChild", node_get, 0, 12),
    JS_CGETSET_MAGIC_DEF("lastChild", node_get, 0, 13),
    JS_CGETSET_MAGIC_DEF("nodeType", node_get, 0, 14),
    JS_CGETSET_MAGIC_DEF("tagName", node_get, 0, 15),
    JS_CGETSET_MAGIC_DEF("childNodes", node_get, 0, 16),
    JS_CGETSET_MAGIC_DEF("children", node_get, 0, 17)};
static const JSCFunctionListEntry document_functions[] = {
    JS_CFUNC_MAGIC_DEF("getElementById", 1, method_find, 0),
    JS_CFUNC_MAGIC_DEF("querySelector", 1, method_find, 1),
    JS_CFUNC_DEF("getElementsByTagName", 1, method_query_all),
    JS_CFUNC_DEF("addEventListener", 2, method_listen),
    JS_CFUNC_MAGIC_DEF("createElement", 1, method_create, 0),
    JS_CFUNC_MAGIC_DEF("createTextNode", 1, method_create, 1),
    JS_CGETSET_MAGIC_DEF("body", document_get, 0, 0),
    JS_CGETSET_MAGIC_DEF("documentElement", document_get, 0, 1),
    JS_CGETSET_MAGIC_DEF("readyState", document_get, 0, 2),
    JS_CGETSET_MAGIC_DEF("title", document_get, document_set, 3),
    JS_CGETSET_MAGIC_DEF("URL", document_get, 0, 4)};
void web_js_reset(void) {
  document_state = 0;
  if (context) {
    for (unsigned i = 0; i < WEB_NODES; i++)
      JS_FreeValue(context, wrappers[i]);
    for (unsigned i = 0; i < handler_count; i++)
      JS_FreeValue(context, handlers[i].function);
    for (unsigned i = 0; i < 32; i++)
      if (timers[i].id)
        JS_FreeValue(context, timers[i].function);
    for (unsigned i = 0; i < 48; i++)
      if (fetches[i].used) {
        JS_FreeValue(context, fetches[i].resolve);
        JS_FreeValue(context, fetches[i].reject);
      }
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
  }
  memset(timers, 0, sizeof(timers));
  memset(fetches, 0, sizeof(fetches));
  handler_count = 0;
  for (unsigned i = 0; i < WEB_NODES; i++)
    wrappers[i] = JS_UNDEFINED;
  runtime = JS_NewRuntime();
  context = runtime ? JS_NewContext(runtime) : 0;
  if (!context) {
    runtime = 0;
    return;
  }
  origin = cycles();
  JS_SetMemoryLimit(runtime, 12 * 1024 * 1024);
  JS_SetMaxStackSize(runtime, 128 * 1024);
  JS_SetInterruptHandler(runtime, interrupt, 0);
  if (!node_class)
    JS_NewClassID(&node_class);
  JSClassDef definition = {.class_name = "Element"};
  JS_NewClass(runtime, node_class, &definition);
  JSValue proto = JS_NewObject(context);
  JS_SetPropertyFunctionList(context, proto, node_functions,
                             sizeof(node_functions) /
                                 sizeof(node_functions[0]));
  JSValue proto_copy = JS_DupValue(context, proto);
  JS_SetClassProto(context, node_class, proto);
  JSValue global = JS_GetGlobalObject(context), doc = JS_NewObject(context);
  JS_SetPropertyStr(context, global, "__ElementPrototype", proto_copy);
  JS_SetPropertyFunctionList(context, doc, document_functions,
                             sizeof(document_functions) /
                                 sizeof(document_functions[0]));
  JS_SetPropertyStr(context, global, "document", doc);
  JS_SetPropertyStr(context, global, "window", JS_DupValue(context, global));
  JS_SetPropertyStr(context, global, "self", JS_DupValue(context, global));
  JS_SetPropertyStr(
      context, global, "addEventListener",
      JS_NewCFunction(context, method_listen, "addEventListener", 2));
  JSValue loc = JS_NewObject(context);
  JS_DefinePropertyGetSet(context, loc, JS_NewAtom(context, "href"),
                          JS_NewCFunction2(context, (JSCFunction *)location_get,
                                           "get href", 0, JS_CFUNC_getter, 0),
                          JS_NewCFunction2(context, (JSCFunction *)location_set,
                                           "set href", 1, JS_CFUNC_setter, 0),
                          JS_PROP_CONFIGURABLE);
  JS_SetPropertyStr(context, loc, "assign",
                    JS_NewCFunction(context, location_assign, "assign", 1));
  JS_SetPropertyStr(context, loc, "replace",
                    JS_NewCFunction(context, location_assign, "replace", 1));
  JS_SetPropertyStr(context, global, "location", loc);
  JSValue console = JS_NewObject(context);
  JS_SetPropertyStr(context, console, "log",
                    JS_NewCFunction(context, console_log, "log", 1));
  JS_SetPropertyStr(context, console, "error",
                    JS_NewCFunction(context, console_log, "error", 1));
  JS_SetPropertyStr(context, console, "warn",
                    JS_NewCFunction(context, console_log, "warn", 1));
  JS_SetPropertyStr(context, global, "console", console);
  JSValue perf = JS_NewObject(context);
  JS_SetPropertyStr(context, perf, "now",
                    JS_NewCFunction(context, performance_now, "now", 0));
  JS_SetPropertyStr(context, global, "performance", perf);
  JSValue navigator = JS_NewObject(context);
  JS_SetPropertyStr(context, navigator, "userAgent",
                    JS_NewString(context, "BETAOS native browser"));
  JS_SetPropertyStr(context, navigator, "language",
                    JS_NewString(context, "es"));
  JS_SetPropertyStr(context, global, "navigator", navigator);
  JS_SetPropertyStr(context, global, "setTimeout",
                    JS_NewCFunctionMagic(context, timer_set, "setTimeout", 2,
                                         JS_CFUNC_generic_magic, 0));
  JS_SetPropertyStr(context, global, "setInterval",
                    JS_NewCFunctionMagic(context, timer_set, "setInterval", 2,
                                         JS_CFUNC_generic_magic, 1));
  JS_SetPropertyStr(context, global, "clearTimeout",
                    JS_NewCFunction(context, timer_clear, "clearTimeout", 1));
  JS_SetPropertyStr(context, global, "clearInterval",
                    JS_NewCFunction(context, timer_clear, "clearInterval", 1));
  JS_SetPropertyStr(context, global, "fetch",
                    JS_NewCFunction(context, fetch_start, "fetch", 1));
  JS_FreeValue(context, global);
  web_js_eval(
      "(()=>{const p=__ElementPrototype;delete "
      "globalThis.__ElementPrototype;globalThis.Element=function "
      "Element(){throw new TypeError('Illegal "
      "constructor')};Element.prototype=p;globalThis.Node=Element;Object."
      "defineProperty(p,'style',{get(){const self=this;return new "
      "Proxy({},{set(o,k,v){if(k==='cssText'){self.setAttribute('style',String("
      "v));return true}const "
      "name=String(k).replace(/[A-Z]/"
      "g,x=>'-'+x.toLowerCase());self.setAttribute('style',(self.getAttribute('"
      "style')||'')+';'+name+':'+v);return "
      "true},get(o,k){if(k==='setProperty')return(name,value)=>{self."
      "setAttribute('style',(self.getAttribute('style')||'')+';'+name+':'+"
      "value)};if(k==='cssText')return self.getAttribute('style')||'';return "
      "''}})}});Object.defineProperty(p,'classList',{get(){const "
      "self=this;return{add(...xs){self.className=[...new "
      "Set((self.className+' '+xs.join(' ')).trim().split(/\\s+/))].join(' "
      "')},remove(...xs){self.className=self.className.split(/\\s+/"
      ").filter(x=>!xs.includes(x)).join(' ')},contains(x){return "
      "self.className.split(/\\s+/"
      ").includes(x)},toggle(x){if(this.contains(x)){this.remove(x);return "
      "false}this.add(x);return true}}}})})()",
      "browser:dom");
  serial("JS: QuickJS runtime ready\n");
}
void web_js_eval(const char *code, const char *url) {
  if (!context)
    return;
  budget();
  report(JS_Eval(context, code, strlen(code), url, JS_EVAL_TYPE_GLOBAL));
}
int web_js_event(int node, const char *type) {
  if (!context)
    return 0;
  budget();
  JSValue event = JS_NewObject(context);
  JS_SetPropertyStr(context, event, "type", JS_NewString(context, type));
  JS_SetPropertyStr(context, event, "target", node_value(node));
  JS_SetPropertyStr(context, event, "defaultPrevented", JS_FALSE);
  JSValue prevent = JS_Eval(context, "(function(){this.defaultPrevented=true})",
                            strlen("(function(){this.defaultPrevented=true})"),
                            "event", JS_EVAL_TYPE_GLOBAL);
  JS_SetPropertyStr(context, event, "preventDefault", prevent);
  for (int p = node, depth = 0; p >= 0 && depth < 64;
       p = web_nodes[p].parent, depth++)
    for (unsigned i = 0; i < handler_count; i++)
      if (handlers[i].node == p && !strcmp(handlers[i].type, type)) {
        JSValue receiver = node_value(p);
        report(JS_Call(context, handlers[i].function, receiver, 1, &event));
        JS_FreeValue(context, receiver);
      }
  JSValue flag = JS_GetPropertyStr(context, event, "defaultPrevented");
  int stopped = JS_ToBool(context, flag);
  JS_FreeValue(context, flag);
  JS_FreeValue(context, event);
  return stopped;
}
void web_js_loaded(void) {
  document_state = 1;
  web_js_event(0, "DOMContentLoaded");
  document_state = 2;
  web_js_event(0, "load");
  web_js_eval("if(typeof window.onload==='function')window.onload({type:'load',target:document});", "browser:load");
}
void web_js_tick(void) {
  if (!context)
    return;
  unsigned count = 0;
  for (unsigned i = 0; i < 32 && count < 4; i++)
    if (timers[i].id && cycles() >= timers[i].due) {
      JSValue fn = JS_DupValue(context, timers[i].function);
      if (timers[i].interval)
        timers[i].due = cycles() + timers[i].interval;
      else {
        timers[i].id = 0;
        JS_FreeValue(context, timers[i].function);
      }
      budget();
      report(JS_Call(context, fn, JS_UNDEFINED, 0, 0));
      JS_FreeValue(context, fn);
      count++;
    }
  for (unsigned i = 0; i < 8; i++) {
    JSContext *ctx;
    budget();
    int result = JS_ExecutePendingJob(runtime, &ctx);
    if (result < 0)
      report(JS_EXCEPTION);
    if (result <= 0)
      break;
  }
}
