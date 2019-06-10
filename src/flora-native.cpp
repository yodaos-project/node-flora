#include "flora-wrap.h"

FloraWrap::AsyncInfo FloraWrap::globalAsync;
napi_ref FloraReply::constructor{nullptr};

char* get_js_string(napi_env env, napi_value str) {
  size_t sz{0};
  napi_get_value_string_utf8(env, str, nullptr, 0, &sz);
  auto res = new char[sz + 1];
  napi_get_value_string_utf8(env, str, res, sz + 1, &sz);
  return res;
}

void release_js_string(char* str) {
  delete[] str;
}

bool genCapsByJSArray(napi_env env, napi_value jsmsg, shared_ptr<Caps>& caps) {
  caps = Caps::new_instance();
  uint32_t len{0};
  uint32_t i;
  napi_value v{nullptr};
  napi_valuetype tp{napi_undefined};

  napi_get_array_length(env, jsmsg, &len);
  for (i = 0; i < len; ++i) {
    napi_get_element(env, jsmsg, i, &v);
    napi_typeof(env, v, &tp);
    if (tp == napi_number) {
      double d{0.0};
      napi_get_value_double(env, v, &d);
      caps->write(d);
    } else if (tp == napi_string) {
      auto str = get_js_string(env, v);
      caps->write(str);
      release_js_string(str);
    } else if (tp == napi_object) {
      bool isArray{false};
      bool isArrayBuffer{false};
      napi_is_array(env, v, &isArray);
      napi_is_arraybuffer(env, v, &isArrayBuffer);
      if (isArray) {
        shared_ptr<Caps> sub;
        if (!genCapsByJSArray(env, v, sub))
          return false;
        caps->write(sub);
      } else if (isArrayBuffer) {
        void* data{nullptr};
        size_t size{0};
        napi_get_arraybuffer_info(env, v, &data, &size);
        caps->write(data, size);
      } else
        return false;
    } else if (tp == napi_undefined) {
      caps->write();
    } else
      return false;
  }
  return true;
}

bool genCapsByJSCaps(napi_env env, napi_value jsmsg, shared_ptr<Caps>& caps) {
  void* ptr = nullptr;
  napi_unwrap(env, jsmsg, &ptr);
  if (ptr == nullptr)
    return false;
  caps = reinterpret_cast<FloraWrap::HackedNativeCaps*>(ptr)->caps;
  return true;
}

static napi_value InitNode(napi_env env, napi_value exports) {
  auto res = NapiWrap<FloraWrap>::defineClass(env, "Agent", {
    { "start", &FloraWrap::start },
    { "nativeSubscribe", &FloraWrap::subscribe },
    { "nativePost", &FloraWrap::post },
    { "nativeGenArray", &FloraWrap::genArray },
    { "close", &FloraWrap::close },
    { "nativeCall", &FloraWrap::call },
    { "nativeDeclareMethod", &FloraWrap::declareMethod }
  });
  FloraWrap::initialize(env);
  auto ctor = NapiWrap<FloraReply>::defineClass(env, "Reply", {
    { "writeCode", &FloraReply::writeCode },
    { "writeData", &FloraReply::writeData },
    { "end", &FloraReply::end }
  });
  napi_create_reference(env, ctor, 1, &FloraReply::constructor);
  return res;
}

NAPI_MODULE(flora, InitNode);
