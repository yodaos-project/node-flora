#pragma once

#include "napi-wrap.h"

char* get_js_string(napi_env env, napi_value str);
void release_js_string(char* str);
bool genCapsByJSArray(napi_env env, napi_value jsmsg, shared_ptr<Caps>& caps);
bool genCapsByJSCaps(napi_env env, napi_value jsmsg, shared_ptr<Caps>& caps);

class FloraReply : public NapiWrap<FloraReply> {
public:
  static napi_value newInstance(napi_env env, shared_ptr<flora::Reply>& reply) {
    napi_value res{nullptr};
    napi_value ctor{nullptr};
    napi_get_reference_value(env, constructor, &ctor);
    if (napi_new_instance(env, ctor, 0, nullptr, &res) == napi_ok) {
      FloraReply* frep{nullptr};
      napi_unwrap(env, res, (void**)&frep);
      if (frep)
        frep->reply = reply;
    }
    return res;
  }

  napi_value newInstance(napi_env env, napi_callback_info cbinfo) {
    napi_value thisPtr{nullptr};
    napi_get_cb_info(env, cbinfo, nullptr, nullptr, &thisPtr, nullptr);
    return thisPtr;
  }

  napi_value writeCode(napi_env env, napi_callback_info cbinfo) {
    size_t argc{1};
    napi_value argv[1]{0};
    napi_get_cb_info(env, cbinfo, &argc, argv, nullptr, nullptr);
    bool invalidParams{false};
    if (argc < 1)
      invalidParams = true;
    else {
      napi_valuetype tp{napi_undefined};
      napi_typeof(env, argv[0], &tp);
      if (tp != napi_number)
        invalidParams = true;
    }
    if (invalidParams) {
      napi_throw_type_error(env, nullptr,
                            "invalid param: should be number");
      return nullptr;
    }
    int32_t code{0};
    napi_get_value_int32(env, argv[0], &code);
    reply->write_code(code);
    return nullptr;
  }

  napi_value writeData(napi_env env, napi_callback_info cbinfo) {
    size_t argc{1};
    napi_value argv[1]{0};
    napi_get_cb_info(env, cbinfo, &argc, argv, nullptr, nullptr);
    napi_valuetype tp{napi_undefined};
    bool isArray{false};
    shared_ptr<Caps> caps;
    if (argc < 1)
      goto invalidParam;
    else {
      napi_typeof(env, argv[0], &tp);
      if (tp != napi_object && tp != napi_external)
        goto invalidParam;
    }
    napi_is_array(env, argv[0], &isArray);
    if (isArray) {
      if (!genCapsByJSArray(env, argv[0], caps))
        goto invalidParam;
    } else {
      if (!genCapsByJSCaps(env, argv[0], caps))
        goto invalidParam;
    }
    reply->write_data(caps);
    return nullptr;

invalidParam:
    napi_throw_type_error(env, nullptr, "invalid param");
    return nullptr;
  }

  napi_value end(napi_env env, napi_callback_info cbinfo) {
    size_t argc{2};
    napi_value argv[2]{0};
    napi_get_cb_info(env, cbinfo, &argc, argv, nullptr, nullptr);
    napi_valuetype tp;
    int32_t code{0};
    shared_ptr<Caps> caps;
    if (argc >= 1) {
      tp = napi_undefined;
      napi_typeof(env, argv[0], &tp);
      if (tp == napi_number)
        napi_get_value_int32(env, argv[0], &code);
    }
    if (argc >= 2) {
      tp = napi_undefined;
      napi_typeof(env, argv[1], &tp);
      if (tp == napi_object || tp == napi_external) {
        bool isArray{false};
        napi_is_array(env, argv[1], &isArray);
        if (isArray) {
          genCapsByJSArray(env, argv[1], caps);
        } else {
          genCapsByJSCaps(env, argv[1], caps);
        }
      }
    }
    reply->end(code, caps);
    return nullptr;
  }

public:
  shared_ptr<flora::Reply> reply;

  static napi_ref constructor;
};
