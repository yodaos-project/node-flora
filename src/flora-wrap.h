#pragma once

#include <node_api.h>
#include "flora-agent.h"
#include "common.h"
#include "napi-wrap.h"
#include "flora-reply.h"

#define FLORA_WRAP_FLAG_DAEMON 1
#define FLORA_WRAP_STATUS_CLEAN 1

using flora::MsgSender;
using flora::Response;
using flora::Reply;

class FloraWrap : public NapiWrap<FloraWrap> {
private:
  class Subscription {
  public:
    string name;
    napi_env env;
    // js arrow function defined in Agent.subscribe
    napi_ref wrapcb;
    // js function 'cb', passed as param for js function 'Agent.subscribe'
    napi_ref cb;
    // flora::Agent pointer address
    intptr_t agentPtr;
  };
  typedef map<uint32_t, Subscription> SubscriptionMap;
  class SenderInfo {
  public:
    uint16_t type;
    uint16_t port;
    uint32_t pid;
    std::string name;
    std::string ipaddr;
  };
  class PostInfo {
  public:
    shared_ptr<Caps> msg;
    uint32_t type;
    // key of SubscriptionMap
    uint32_t subKey;
    SenderInfo sender;
  };
  typedef list<PostInfo> PostInfoList;
  class Caller {
  public:
    napi_env env;
    napi_ref cb;
    // flora::Agent pointer address
    intptr_t agentPtr;
  };
  typedef map<uint32_t, Caller> CallerMap;
  class ReturnInfo {
  public:
    uint32_t callerId;
    int32_t resCode;
    int32_t retCode;
    shared_ptr<Caps> msg;
  };
  typedef list<ReturnInfo> ReturnInfoList;
  class Callee {
  public:
    napi_env env;
    napi_ref cb;
    // flora::Agent pointer address
    intptr_t agentPtr;
  };
  typedef map<string, Callee> CalleeMap;
  class CallInfo {
  public:
    string name;
    shared_ptr<Caps> msg;
    shared_ptr<Reply> reply;
    SenderInfo sender;
  };
  typedef list<CallInfo> CallInfoList;
  class AsyncInfo {
  public:
    uv_async_t postAsync;
    uv_async_t returnAsync;
    uv_async_t callAsync;
    SubscriptionMap subscriptions;
    CallerMap callers;
    CalleeMap callees;
    PostInfoList pendingPosts;
    ReturnInfoList pendingReturns;
    CallInfoList pendingCalls;
    uint32_t subseq{0};
    uint32_t callerseq{0};
    uint32_t calleeseq{0};
    mutex listMutex;
    napi_async_context asyncContext;

    uint32_t subscribe(const char* name, napi_env env, napi_ref wrapcb,
                   napi_ref cb, intptr_t ptr) {
      auto key = ++subseq;
      Subscription sub;
      sub.name = name;
      sub.env = env;
      sub.wrapcb = wrapcb;
      sub.cb = cb;
      sub.agentPtr = ptr;
      auto val = make_pair(key, sub);
      subscriptions.insert(val);
      return key;
    }

    void newPendingPost(uint32_t key, shared_ptr<Caps>& msg, uint32_t type) {
      listMutex.lock();
      pendingPosts.emplace_back();
      auto it = pendingPosts.rbegin();
      it->subKey = key;
      it->msg = msg;
      it->type = type;
      createSenderInfo(it->sender);
      listMutex.unlock();
      uv_async_send(&postAsync);
    }

    void unsubscribe(intptr_t ptr) {
      auto it = subscriptions.begin();
      while (it != subscriptions.end()) {
        if (it->second.agentPtr == ptr)
          it = subscriptions.erase(it);
        else
          ++it;
      }
    }

    uint32_t newCallerId() {
      return ++callerseq;
    }

    void newCaller(uint32_t key, napi_env env, napi_ref cb, intptr_t ptr) {
      Caller caller;
      caller.env = env;
      caller.cb = cb;
      caller.agentPtr = ptr;
      callers.insert(make_pair(key, caller));
    }

    void eraseCaller(intptr_t ptr) {
      auto it = callers.begin();
      while (it != callers.end()) {
        if (it->second.agentPtr == ptr)
          it = callers.erase(it);
        else
          ++it;
      }
    }

    void newPendingReturn(uint32_t callerId, int32_t resCode, Response& resp) {
      listMutex.lock();
      pendingReturns.emplace_back();
      auto it = pendingReturns.rbegin();
      it->callerId = callerId;
      it->resCode = resCode;
      it->retCode = resp.ret_code;
      it->msg = resp.data;
      listMutex.unlock();
      uv_async_send(&returnAsync);
    }

    void declareMethod(const char* name, napi_env env, napi_ref cb,
                       intptr_t ptr) {
      Callee callee;
      callee.env = env;
      callee.cb = cb;
      callee.agentPtr = ptr;
      callees.insert(make_pair(name, callee));
    }

    void removeMethod(intptr_t ptr) {
      auto it = callees.begin();
      while (it != callees.end()) {
        if (it->second.agentPtr == ptr)
          it = callees.erase(it);
        else
          ++it;
      }
    }

    void newPendingCall(const char* name, shared_ptr<Caps>& msg,
                        shared_ptr<Reply>& reply) {
      listMutex.lock();
      pendingCalls.emplace_back();
      auto it = pendingCalls.rbegin();
      it->name = name;
      it->msg = msg;
      it->reply = reply;
      createSenderInfo(it->sender);
      listMutex.unlock();
      uv_async_send(&callAsync);
    }
  };

public:
  class HackedNativeCaps {
   public:
    std::shared_ptr<Caps> caps;
  };

  FloraWrap() {
    KLOGD(TAG, "FloraWrap construct, %p", this);
  }

  ~FloraWrap() {
    close();
    KLOGD(TAG, "FloraWrap destruct, %p", this);
  }

  napi_value newInstance(napi_env env, napi_callback_info cbinfo) {
    bool invalidParam = false;
    size_t argc = 2;
    napi_value value;
    napi_value argv[2];
    auto r = napi_get_cb_info(env, cbinfo, &argc, argv, &value, nullptr);
    if (r != napi_ok)
      return nullptr;
    if (argc == 0)
      invalidParam = true;
    else {
      napi_valuetype type{napi_undefined};
      napi_typeof(env, argv[0], &type);
      if (type != napi_string)
        invalidParam = true;
    }
    if (invalidParam) {
      napi_throw_type_error(env, nullptr,
                            "must specify first string param 'uri'");
      return nullptr;
    }
    char uri[64]{0};
    size_t len;
    napi_get_value_string_utf8(env, argv[0], uri, sizeof(uri), &len);
    floraUri = uri;
    if (argc >= 2)
      parseOptions(env, argv[1]);
    return value;
  }

  void parseOptions(napi_env env, napi_value jsopts) {
    napi_valuetype type{napi_undefined};
    napi_typeof(env, jsopts, &type);
    if (type == napi_object) {
      napi_value val;
      auto r = napi_get_named_property(env, jsopts, "reconnInterval", &val);
      if (r == napi_ok)
        napi_get_value_uint32(env, val, &options.reconnInterval);
      r = napi_get_named_property(env, jsopts, "bufsize", &val);
      if (r == napi_ok)
        napi_get_value_uint32(env, val, &options.bufsize);
      r = napi_get_named_property(env, jsopts, "beepInterval", &val);
      if (r == napi_ok)
        napi_get_value_uint32(env, val, &options.beepInterval);
      r = napi_get_named_property(env, jsopts, "norespTimeout", &val);
      if (r == napi_ok)
        napi_get_value_uint32(env, val, &options.norespTimeout);
      r = napi_get_named_property(env, jsopts, "daemon", &val);
      if (r == napi_ok) {
        bool b{false};
        napi_get_value_bool(env, val, &b);
        if (b)
          options.flags |= FLORA_WRAP_FLAG_DAEMON;
      }
    }
    KLOGD(TAG, "reconnect interval %u", options.reconnInterval);
    KLOGD(TAG, "bufsize %u", options.bufsize);
    KLOGD(TAG, "beepInterval %u", options.beepInterval);
    KLOGD(TAG, "norespTimeout %u", options.norespTimeout);
    KLOGD(TAG, "flags 0x%x", options.flags);
  }

  napi_value start(napi_env env, napi_callback_info info) {
    KLOGD(TAG, "native start invoked!!  %p", this);
    status = 0;
    if ((options.flags & FLORA_WRAP_FLAG_DAEMON) == 0)
      uv_ref((uv_handle_t*)&globalAsync.postAsync);
    agent.config(FLORA_AGENT_CONFIG_URI, floraUri.c_str());
    agent.config(FLORA_AGENT_CONFIG_BUFSIZE, options.bufsize);
    agent.config(FLORA_AGENT_CONFIG_RECONN_INTERVAL, options.reconnInterval);
    agent.config(FLORA_AGENT_CONFIG_KEEPALIVE, options.beepInterval,
                 options.norespTimeout);
    agent.start();
    return nullptr;
  }

  napi_value subscribe(napi_env env, napi_callback_info info) {
    KLOGD(TAG, "native subscribe invoked!!");
    size_t argc{3};
    napi_value argv[3];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    bool invalidParam{false};
    napi_valuetype type{napi_undefined};
    napi_typeof(env, argv[0], &type);
    if (type != napi_string)
      invalidParam = true;
    napi_typeof(env, argv[2], &type);
    if (type != napi_function)
      invalidParam = true;
    if (invalidParam) {
      napi_throw_type_error(env, nullptr, "invalid params");
      return nullptr;
    }
    auto name = get_js_string(env, argv[0]);
    napi_ref wrapcb{nullptr};
    napi_create_reference(env, argv[1], 1, &wrapcb);
    napi_ref cb{nullptr};
    napi_create_reference(env, argv[2], 1, &cb);
    auto key = globalAsync.subscribe(name, env, wrapcb, cb, (intptr_t)this);
    agent.subscribe(name, [key](const char*, shared_ptr<Caps>& msg, uint32_t type) {
      FloraWrap::globalAsync.newPendingPost(key, msg, type);
    });
    release_js_string(name);
    return nullptr;
  }

  napi_value post(napi_env env, napi_callback_info info) {
    KLOGD(TAG, "native post invoked!! %p", this);
    size_t argc{4};
    napi_value argv[4];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    auto name = get_js_string(env, argv[0]);
    shared_ptr<Caps> msg;
    bool isCaps{false};
    napi_get_value_bool(env, argv[3], &isCaps);
    if (isCaps)
      genCapsByJSCaps(env, argv[1], msg);
    else {
      bool isArray{false};
      napi_is_array(env, argv[1], &isArray);
      if (isArray)
        genCapsByJSArray(env, argv[1], msg);
    }
    uint32_t type{FLORA_MSGTYPE_INSTANT};
    napi_get_value_uint32(env, argv[2], &type);
    auto r = agent.post(name, msg, type);
    release_js_string(name);
    napi_value res{nullptr};
    napi_create_int32(env, r, &res);
    return res;
  }

  napi_value genArray(napi_env env, napi_callback_info info) {
    KLOGD(TAG, "native gen array invoked!!");
    size_t argc{1};
    napi_value argv[1];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    HackedNativeCaps* hackedCaps{nullptr};
    napi_get_value_external(env, argv[0], (void**)&hackedCaps);
    return genJSArrayByCaps(env, hackedCaps->caps);
  }

  napi_value close(napi_env env, napi_callback_info info) {
    KLOGD(TAG, "native close invoked!!  %p", this);
    close();
    return nullptr;
  }

  napi_value call(napi_env env, napi_callback_info info) {
    KLOGD(TAG, "native call invoked!! %p", this);
    size_t argc{6};
    // 0: name
    // 1: msg
    // 2: target
    // 3: cb
    // 4: isCaps
    // 5: timeout
    napi_value argv[6];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    auto name = get_js_string(env, argv[0]);
    shared_ptr<Caps> msg;
    bool isCaps{false};
    napi_get_value_bool(env, argv[4], &isCaps);
    if (isCaps)
      genCapsByJSCaps(env, argv[1], msg);
    else {
      bool isArray{false};
      napi_is_array(env, argv[1], &isArray);
      if (isArray)
        genCapsByJSArray(env, argv[1], msg);
    }
    auto target = get_js_string(env, argv[2]);
    napi_valuetype tp{napi_undefined};
    napi_typeof(env, argv[5], &tp);
    uint32_t timeout{0};
    if (tp == napi_number)
      napi_get_value_uint32(env, argv[5], &timeout);
    uint32_t callerId = globalAsync.newCallerId();
    auto r = agent.call(name, msg, target, [callerId](int32_t resCode, Response& resp) {
      globalAsync.newPendingReturn(callerId, resCode, resp);
    }, timeout);
    if (r == FLORA_CLI_SUCCESS) {
      napi_ref cbref{nullptr};
      napi_create_reference(env, argv[3], 1, &cbref);
      globalAsync.newCaller(callerId, env, cbref, (intptr_t)this);
    }
    release_js_string(target);
    release_js_string(name);
    napi_value res{nullptr};
    napi_create_int32(env, r, &res);
    return res;
  }

  napi_value declareMethod(napi_env env, napi_callback_info info) {
    KLOGD(TAG, "native declareMethod invoked!!");
    size_t argc{2};
    napi_value argv[2];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    bool invalidParam{false};
    napi_valuetype type{napi_undefined};
    napi_typeof(env, argv[0], &type);
    if (type != napi_string)
      invalidParam = true;
    if (invalidParam) {
      napi_throw_type_error(env, nullptr, "invalid params");
      return nullptr;
    }
    auto name = get_js_string(env, argv[0]);
    napi_ref cb{nullptr};
    napi_create_reference(env, argv[1], 1, &cb);
    globalAsync.declareMethod(name, env, cb, (intptr_t)this);
    agent.declare_method(name, [](const char* name, shared_ptr<Caps>& msg, shared_ptr<Reply>& reply) {
      FloraWrap::globalAsync.newPendingCall(name, msg, reply);
    });
    release_js_string(name);
    return nullptr;
  }

  napi_value unsubscribe(napi_env env, napi_callback_info info) {
    return nullptr;
  }

  napi_value removeMethod(napi_env env, napi_callback_info info) {
    return nullptr;
  }

  static void initialize(napi_env env) {
    uv_loop_s* loop;
    napi_get_uv_event_loop(env, &loop);
    uv_async_init(loop, &globalAsync.postAsync, postCallback);
    uv_unref((uv_handle_t*)&globalAsync.postAsync);
    uv_async_init(loop, &globalAsync.returnAsync, returnCallback);
    uv_unref((uv_handle_t*)&globalAsync.returnAsync);
    uv_async_init(loop, &globalAsync.callAsync, callCallback);
    uv_unref((uv_handle_t*)&globalAsync.callAsync);
    napi_value resName;
    napi_create_string_utf8(env, "flora-agent", NAPI_AUTO_LENGTH, &resName);
    napi_async_init(env, nullptr, resName, &globalAsync.asyncContext);
  }

private:
  static void postCallback(uv_async_t* async) {
    PostInfoList pendingPosts;
    globalAsync.listMutex.lock();
    pendingPosts.swap(globalAsync.pendingPosts);
    globalAsync.listMutex.unlock();
    for_each(pendingPosts.begin(), pendingPosts.end(), [](PostInfo& info) {
      auto it = FloraWrap::globalAsync.subscriptions.find(info.subKey);
      if (it != FloraWrap::globalAsync.subscriptions.end()) {
        napi_value global{nullptr}, cb{nullptr};
        napi_handle_scope scope;
        napi_open_handle_scope(it->second.env, &scope);
        napi_get_global(it->second.env, &global);
        napi_get_reference_value(it->second.env, it->second.wrapcb, &cb);
        napi_value argv[3]{0};
        createCallbackArgs(it->second.env, info, argv);
        napi_value res;
        napi_make_callback(it->second.env,
            FloraWrap::globalAsync.asyncContext, global, cb, 3, argv, &res);
        napi_close_handle_scope(it->second.env, scope);
      }
    });
  }

  static void returnCallback(uv_async_t* async) {
    ReturnInfoList pendingReturns;
    globalAsync.listMutex.lock();
    pendingReturns.swap(globalAsync.pendingReturns);
    globalAsync.listMutex.unlock();
    for_each(pendingReturns.begin(), pendingReturns.end(), [](ReturnInfo& info) {
      auto it = FloraWrap::globalAsync.callers.find(info.callerId);
      if (it != FloraWrap::globalAsync.callers.end()) {
        napi_value global{nullptr}, cb{nullptr};
        napi_handle_scope scope;
        napi_open_handle_scope(it->second.env, &scope);
        napi_get_global(it->second.env, &global);
        napi_get_reference_value(it->second.env, it->second.cb, &cb);
        napi_value argv[2]{0};
        createCallbackArgs(it->second.env, info, argv);
        napi_value res;
        napi_make_callback(it->second.env,
            FloraWrap::globalAsync.asyncContext, global, cb, 2, argv, &res);
        napi_close_handle_scope(it->second.env, scope);
      }
    });
  }

  static void callCallback(uv_async_t* async) {
    CallInfoList pendingCalls;
    globalAsync.listMutex.lock();
    pendingCalls.swap(globalAsync.pendingCalls);
    globalAsync.listMutex.unlock();
    for_each(pendingCalls.begin(), pendingCalls.end(), [](CallInfo& info) {
      auto it = FloraWrap::globalAsync.callees.find(info.name);
      if (it != FloraWrap::globalAsync.callees.end()) {
        napi_value global{nullptr}, cb{nullptr};
        napi_handle_scope scope;
        napi_open_handle_scope(it->second.env, &scope);
        napi_get_global(it->second.env, &global);
        napi_get_reference_value(it->second.env, it->second.cb, &cb);
        napi_value argv[3]{0};
        createCallbackArgs(it->second.env, info, argv);
        napi_value res;
        napi_make_callback(it->second.env,
            FloraWrap::globalAsync.asyncContext, global, cb, 3, argv, &res);
        napi_close_handle_scope(it->second.env, scope);
      }
    });
  }

  static void createCallbackArgs(napi_env env, PostInfo& info, napi_value* argv) {
    argv[0] = genHackedCaps(env, info.msg);
    napi_create_uint32(env, info.type, argv + 1);
    argv[2] = genSender(env, info.sender);
  }

  static void createCallbackArgs(napi_env env, ReturnInfo& info, napi_value* argv) {
    napi_create_int32(env, info.resCode, argv);
    napi_value resp{nullptr};
    napi_create_object(env, &resp);
    napi_value val;
    napi_create_int32(env, info.retCode, &val);
    napi_set_named_property(env, resp, "retCode", val);
    val = genHackedCaps(env, info.msg);
    napi_set_named_property(env, resp, "msg", val);
    argv[1] = resp;
  }

  static void createCallbackArgs(napi_env env, CallInfo& info, napi_value* argv) {
    argv[0] = genHackedCaps(env, info.msg);
    argv[1] = FloraReply::newInstance(env, info.reply);
    argv[2] = genSender(env, info.sender);
  }

  static napi_value genHackedCaps(napi_env env, shared_ptr<Caps> msg) {
    napi_value jsobj;
    HackedNativeCaps* hackedCaps = new HackedNativeCaps();
    hackedCaps->caps = msg;
    auto r = napi_create_external(env, hackedCaps, freeHackedCaps, nullptr,
                                  &jsobj);
    if (r != napi_ok) {
      delete hackedCaps;
      return nullptr;
    }
    return jsobj;
  }

  static void freeHackedCaps(napi_env, void* data, void* arg) {
    delete reinterpret_cast<HackedNativeCaps*>(data);
  }

  static void createSenderInfo(SenderInfo& sender) {
    sender.type = MsgSender::connection_type();
    if (sender.type == 0)
      sender.pid = MsgSender::pid();
    else {
      sender.ipaddr = MsgSender::ipaddr();
      sender.port = MsgSender::port();
    }
    sender.name = MsgSender::name();
  }

  static napi_value genSender(napi_env env, SenderInfo& sender) {
    napi_value res;
    napi_value val;
    napi_create_object(env, &res);
    if (sender.type == 0) {
      napi_create_uint32(env, sender.pid, &val);
      napi_set_named_property(env, res, "pid", val);
    } else {
      napi_create_string_utf8(env, sender.ipaddr.c_str(),
                              sender.ipaddr.length(), &val);
      napi_set_named_property(env, res, "ipaddr", val);
      napi_create_uint32(env, sender.port, &val);
      napi_set_named_property(env, res, "port", val);
    }
    napi_create_string_utf8(env, sender.name.c_str(),
                            sender.name.length(), &val);
    napi_set_named_property(env, res, "name", val);
    return res;
  }

  static napi_value genJSArrayByCaps(napi_env env, std::shared_ptr<Caps>& msg) {
    napi_value ret{nullptr};
    int32_t iv;
    int64_t lv;
    float fv;
    double dv;
    string sv;
    vector<uint8_t> bv;
    shared_ptr<Caps> cv;
    uint32_t idx = 0;
    napi_value jv;
    void* data;

    if (msg.get() == nullptr)
      return nullptr;
    napi_create_array(env, &ret);
    if (ret == nullptr)
      return nullptr;
    while (true) {
      int32_t mtp = msg->next_type();
      if (mtp == CAPS_ERR_EOO)
        break;
      jv = nullptr;
      switch (mtp) {
      case CAPS_MEMBER_TYPE_INTEGER:
        msg->read(iv);
        napi_create_int32(env, iv, &jv);
        napi_set_element(env, ret, idx++, jv);
        break;
      case CAPS_MEMBER_TYPE_LONG:
        msg->read(lv);
        napi_create_int64(env, lv, &jv);
        napi_set_element(env, ret, idx++, jv);
        break;
      case CAPS_MEMBER_TYPE_FLOAT:
        msg->read(fv);
        napi_create_double(env, fv, &jv);
        napi_set_element(env, ret, idx++, jv);
        break;
      case CAPS_MEMBER_TYPE_DOUBLE:
        msg->read(dv);
        napi_create_double(env, dv, &jv);
        napi_set_element(env, ret, idx++, jv);
        break;
      case CAPS_MEMBER_TYPE_STRING:
        msg->read(sv);
        napi_create_string_utf8(env, sv.c_str(), sv.length(), &jv);
        napi_set_element(env, ret, idx++, jv);
        break;
      case CAPS_MEMBER_TYPE_BINARY:
        msg->read(bv);
        data = nullptr;
        napi_create_arraybuffer(env, bv.size(), &data, &jv);
        if (data)
          memcpy(data, bv.data(), bv.size());
        napi_set_element(env, ret, idx++, jv);
        break;
      case CAPS_MEMBER_TYPE_OBJECT:
        msg->read(cv);
        jv = genJSArrayByCaps(env, cv);
        napi_set_element(env, ret, idx++, jv);
        break;
      case CAPS_MEMBER_TYPE_VOID:
        msg->read();
        napi_set_element(env, ret, idx++, nullptr);
        break;
      }
    }
    return ret;
  }

  void close() {
    if ((status & FLORA_WRAP_STATUS_CLEAN) == 0) {
      agent.close();
      globalAsync.unsubscribe((intptr_t)this);
      globalAsync.eraseCaller((intptr_t)this);
      globalAsync.removeMethod((intptr_t)this);
      KLOGD(TAG, "remain subscriptions %d", globalAsync.subscriptions.size());
      if ((options.flags & FLORA_WRAP_FLAG_DAEMON) == 0)
        uv_unref((uv_handle_t*)&globalAsync.postAsync);
      status |= FLORA_WRAP_STATUS_CLEAN;
    }
  }

private:
  class Options {
  public:
    uint32_t reconnInterval{10000};
    uint32_t bufsize{32768};
    uint32_t beepInterval{50000};
    uint32_t norespTimeout{100000};
    // FLAG_DAEMON: uv_async reference down,
    //              process can exit if not invoke Agent::close
    uint32_t flags{0};
  };
  Options options;
  flora::Agent agent;
  string floraUri;
  uint32_t status{0};

  // for all native Agents
  static AsyncInfo globalAsync;
};
