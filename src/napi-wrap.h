#pragma once

#include <vector>
#include <node_api.h>

template <typename C>
class NapiWrap {
public:
  typedef napi_value (C::*NativeWrapFunc)(napi_env, napi_callback_info);

  class InstanceMethod {
  public:
    std::string name;
    NativeWrapFunc func;
  };
  class InstanceMethodData {
  public:
    NativeWrapFunc func;
  };

  ~NapiWrap() {
    napi_delete_reference(bindEnv, thisRef);
  }

  static napi_value defineClass(napi_env env, const char* name,
      std::initializer_list<InstanceMethod> methods) {
    std::vector<napi_property_descriptor> descs;
    auto methodDataArray = new InstanceMethodData[methods.size()];
    auto it = methods.begin();
    uint32_t idx{0};
    while (it != methods.end()) {
      methodDataArray[idx].func = it->func;
      descs.push_back({ it->name.c_str(), nullptr, nativeMethodTemp, nullptr,
                      nullptr, nullptr, napi_default,
                      (void*)(methodDataArray + idx) });
      ++idx;
      ++it;
    }
    napi_value res{nullptr};
    napi_define_class(env, name, NAPI_AUTO_LENGTH, newInstance, nullptr,
                      descs.size(), descs.data(), &res);
    return res;
  }

  static bool bind(napi_env env, napi_value obj, C* ptr) {
    auto r = napi_wrap(env, obj, ptr, destroyInstance, nullptr, &ptr->thisRef);
    if (r == napi_ok) {
      ptr->bindEnv = env;
      return true;
    }
    return false;
  }

private:
  static napi_value nativeMethodTemp(napi_env env, napi_callback_info cbinfo) {
    napi_value thisPtr;
    InstanceMethodData* methodData{nullptr};
    napi_get_cb_info(env, cbinfo, nullptr, nullptr, &thisPtr, (void**)&methodData);
    C* wrap{nullptr};
    napi_unwrap(env, thisPtr, (void**)&wrap);
    if (wrap)
      return (wrap->*(methodData->func))(env, cbinfo);
    return nullptr;
  }

  static napi_value newInstance(napi_env env, napi_callback_info cbinfo) {
    napi_value value;
    auto r = napi_get_new_target(env, cbinfo, &value);
    if (r != napi_ok)
      return nullptr;
    if (value == nullptr) {
      napi_throw_type_error(env, nullptr,
                            "Class constructors cannot be invoked without 'new'");
      return nullptr;
    }
    r = napi_get_cb_info(env, cbinfo, nullptr, nullptr, &value, nullptr);
    if (r != napi_ok)
      return nullptr;
    auto nativeInst = new C();
    KLOGD(TAG, "newInstance %p", nativeInst);
    auto ret = nativeInst->newInstance(env, cbinfo);
    bool isException{false};
    napi_is_exception_pending(env, &isException);
    if (isException) {
      delete nativeInst;
      return nullptr;
    }
    if (!bind(env, ret, nativeInst)) {
      delete nativeInst;
      return nullptr;
    }
    return ret;
  }

  static void destroyInstance(napi_env env, void* data, void* hint) {
    KLOGD(TAG, "destroyInstance %p", data);
    delete (C*)data;
  }

private:
  napi_ref thisRef;
  napi_env bindEnv;
};
