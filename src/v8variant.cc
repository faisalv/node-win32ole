/*
  v8variant.cc
*/

#include "v8variant.h"
#include "v8dispatch.h"
#include "v8dispmember.h"
#include <node.h>
#include <nan.h>

using namespace v8;
using namespace ole32core;

namespace node_win32ole {

#if(DEBUG)
#define OLETRACEVT(th) do{ \
    V8Variant *v8v = V8Variant::Unwrap<V8Variant>(th); \
    if(!v8v){ std::cerr << "*** V8Variant is NULL ***"; std::cerr.flush(); } \
    CHECK_V8(V8Variant, v8v); \
    OCVariant *ocv = &v8v->ocv; \
    std::cerr << "0x" << std::setw(8) << std::left << std::hex << ocv << ":"; \
    std::cerr << "vt=" << ocv->v.vt << ":"; \
    std::cerr.flush(); \
  }while(0)
#define OLETRACEVT_UNDEFINED(th) do{ \
    V8Variant *v8v = V8Variant::Unwrap<V8Variant>(th); \
    if(!v8v){ std::cerr << "*** V8Variant is NULL ***"; std::cerr.flush(); } \
    CHECK_V8_UNDEFINED(V8Variant, v8v); \
    OCVariant *ocv = &v8v->ocv; \
    std::cerr << "0x" << std::setw(8) << std::left << std::hex << ocv << ":"; \
    std::cerr << "vt=" << ocv->v.vt << ":"; \
    std::cerr.flush(); \
  }while(0)
#else
#define OLETRACEVT(th)
#define OLETRACEVT_UNDEFINED(th)
#endif

Handle<Value> NewOleException(HRESULT hr)
{
  Handle<String> hMsg = Nan::New<String>((const uint16_t*)errorFromCodeW(hr).c_str()).ToLocalChecked();
  Local<v8::Value> args[2] = { Nan::New<Uint32>(hr), hMsg };
  int argc = sizeof(args) / sizeof(args[0]); // == 2
  Local<Object> target = Nan::New(module_target);
  Handle<Function> function = Handle<Function>::Cast(GET_PROP(target, "OLEException").ToLocalChecked());
  return Nan::NewInstance(function, argc, args).ToLocalChecked();
}

Handle<Value> NewOleException(HRESULT hr, const ErrorInfo& info)
{
  Handle<String> hMsg;
  if (info.sDescription.empty())
  {
    hMsg = Nan::New<String>((const uint16_t*)errorFromCodeW(hr).c_str()).ToLocalChecked();
  } else {
    hMsg = Nan::New<String>((const uint16_t*)info.sDescription.c_str()).ToLocalChecked();
  }
  if (info.scode)
  {
    hr = HRESULT_FROM_WIN32(info.scode);
  }
  else if (info.wCode)
  {
    hr = info.wCode; // likely a private error code
  }
  Local<v8::Value> args[2] = { Nan::New<Uint32>(hr), hMsg };
  Local<Object> target = Nan::New(module_target);
  Handle<Function> function = Handle<Function>::Cast(GET_PROP(target, "OLEException").ToLocalChecked());
  Handle<Object> errorObj = Nan::NewInstance(function, 2, args).ToLocalChecked();
  if (!info.sSource.empty())
  {
    Nan::Set(errorObj, Nan::New("source").ToLocalChecked(), Nan::New((const uint16_t*)info.sSource.c_str()).ToLocalChecked());
  }
  if (!info.sHelpFile.empty())
  {
    Nan::Set(errorObj, Nan::New("helpFile").ToLocalChecked(), Nan::New((const uint16_t*)info.sHelpFile.c_str()).ToLocalChecked());
  }
  if (info.dwHelpContext)
  {
    Nan::Set(errorObj, Nan::New("helpContext").ToLocalChecked(), Nan::New((uint32_t)info.dwHelpContext));
  }
  return errorObj;
}

Nan::Persistent<FunctionTemplate> V8Variant::clazz;

void V8Variant::Init(Nan::ADDON_REGISTER_FUNCTION_ARGS_TYPE target)
{
  Nan::HandleScope scope;
  Local<FunctionTemplate> t = Nan::New<FunctionTemplate>(New);
  t->InstanceTemplate()->SetInternalFieldCount(1);
  t->SetClassName(Nan::New("V8Variant").ToLocalChecked());
  Nan::SetPrototypeMethod(t, "isA", OLEIsA);
  Nan::SetPrototypeMethod(t, "vtName", OLEVTName);
  Nan::SetPrototypeMethod(t, "toBoolean", OLEBoolean);
  Nan::SetPrototypeMethod(t, "toInt32", OLEInt32);
  Nan::SetPrototypeMethod(t, "toNumber", OLENumber);
  Nan::SetPrototypeMethod(t, "toDate", OLEDate);
  Nan::SetPrototypeMethod(t, "toUtf8", OLEUtf8);
  Nan::SetPrototypeMethod(t, "toValue", OLEValue);
  Nan::SetPrototypeMethod(t, "valueOf", OLEValue);
  Nan::SetPrototypeMethod(t, "toString", OLEStringValue);
  Nan::SetPrototypeMethod(t, "toLocaleString", OLELocaleStringValue);
//  Nan::SetPrototypeMethod(t, "New", New);
/*
 In ParseUnaryExpression() < v8/src/parser.cc >
 v8::Object::ToBoolean() is called directly for unary operator '!'
 instead of v8::Object::valueOf()
 so NamedPropertyHandler will not be called
 Local<Boolean> ToBoolean(); // How to fake ? override v8::Value::ToBoolean
*/
  Nan::SetPrototypeMethod(t, "Finalize", Finalize);
  Nan::Set(target, Nan::New("V8Variant").ToLocalChecked(), t->GetFunction());
  clazz.Reset(t);
}

OCVariant *V8Variant::ValueToVariant(Handle<Value> v)
{
  if (v->IsNull() || v->IsUndefined()) {
    // todo: make separate undefined type
    return new OCVariant();
  }
  if (v.IsEmpty() || v->IsExternal() || v->IsNativeError() || v->IsFunction())
  {
    Nan::ThrowTypeError("Cannot interpret this value as a valid OLE value (bad value class)");
    return NULL;
  }
// VT_USERDEFINED VT_VARIANT VT_BYREF VT_ARRAY more...
  if(v->IsBoolean() || v->IsBooleanObject()){
    return new OCVariant(Nan::To<bool>(v).FromJust());
  }else if(v->IsArray()){
// VT_BYREF VT_ARRAY VT_SAFEARRAY
    Nan::ThrowTypeError("Passing Arrays to OLE not currently supported");
    return NULL;
  }else if(v->IsInt32()){
    return new OCVariant((long)Nan::To<int32_t>(v).FromJust());
  }else if(v->IsUint32()){
    return new OCVariant((long)Nan::To<uint32_t>(v).FromJust(), VT_UI4);
  }else if(v->IsNumber() || v->IsNumberObject()){
    return new OCVariant(Nan::To<double>(v).FromJust()); // double
  }else if(v->IsDate()){
    double d = Nan::To<double>(v).FromJust();
    time_t sec = (time_t)(d / 1000.0);
    int msec = (int)(d - sec * 1000.0);
    struct tm *t = localtime(&sec); // *** must check locale ***
    if(!t){
      Nan::ThrowTypeError("Saw a Date, but couldn't convert it to an OLE value");
      return NULL;
    }
    SYSTEMTIME syst;
    syst.wYear = t->tm_year + 1900;
    syst.wMonth = t->tm_mon + 1;
    syst.wDay = t->tm_mday;
    syst.wHour = t->tm_hour;
    syst.wMinute = t->tm_min;
    syst.wSecond = t->tm_sec;
    syst.wMilliseconds = msec;
    SystemTimeToVariantTime(&syst, &d);
    return new OCVariant(d, VT_DATE); // date
  }else if(v->IsRegExp()){
    std::cerr << "[RegExp (bug?)]" << std::endl;
    std::cerr.flush();
    return new OCVariant((const wchar_t*)*String::Value(v->ToDetailString()));
  }else if(v->IsString() || v->IsStringObject()){
    return new OCVariant((const wchar_t*)*String::Value(v));
  }else if(v->IsObject()){
    Local<Object> vObj = Local<Object>::Cast(v);
    Local<FunctionTemplate> v8VariantClazz = Nan::New(V8Variant::clazz);
    if (!v8VariantClazz->HasInstance(v))
    {
      V8Variant *v8v = V8Variant::Unwrap<V8Variant>(vObj);
      if (!v8v) {
        Nan::ThrowTypeError("Saw a V8Variant object, but couldn't pull private data");
        return NULL;
      }
      // std::cerr << ocv->v.vt;
      return new OCVariant(v8v->ocv);
    }
    Local<FunctionTemplate> v8DispatchClazz = Nan::New(V8Dispatch::clazz);
    if (!v8DispatchClazz->HasInstance(v))
    {
      V8Dispatch *v8d = V8Dispatch::Unwrap<V8Dispatch>(vObj);
      if (!v8d) {
        Nan::ThrowTypeError("Saw a V8Dispatch object, but couldn't pull private data");
        return NULL;
      }
      // std::cerr << ocv->v.vt;
      return new OCVariant(v8d->ocd.disp);
    }
    Local<FunctionTemplate> v8DispMemberClazz = Nan::New(V8DispMember::clazz);
    if (!v8DispMemberClazz->HasInstance(v))
    {
      Handle<Value> innerResult = INSTANCE_CALL(vObj, "toValue", 0, NULL);
	  return ValueToVariant(innerResult);
	}
  }
  Handle<Value> hFromString = INSTANCE_CALL(Nan::To<Object>(v).ToLocalChecked(), "toString", 0, NULL);
  if (!hFromString->IsString() && !hFromString->IsStringObject())
  {
    Nan::ThrowTypeError("Cannot interpret this value as a valid OLE value");
  } else {
    String::Utf8Value utfValue(Nan::To<String>(hFromString).ToLocalChecked());
    Nan::ThrowTypeError(("Cannot interpret " + std::string((const char*)*utfValue) + " as a valid OLE value").c_str());
  }
  return NULL;
}

NAN_METHOD(V8Variant::OLEIsA)
{
  DISPFUNCIN();
  V8Variant *v8v = V8Variant::Unwrap<V8Variant>(info.This());
  CHECK_V8(V8Variant,v8v);
  DISPFUNCOUT();
  return info.GetReturnValue().Set(Nan::New(v8v->ocv.v.vt));
}

NAN_METHOD(V8Variant::OLEVTName)
{
  DISPFUNCIN();
  V8Variant *v8v = V8Variant::Unwrap<V8Variant>(info.This());
  CHECK_V8(V8Variant, v8v);
  Local<Object> target = Nan::New(module_target);
  Array *a = Array::Cast(*(GET_PROP(target, "vt_names").ToLocalChecked()));
  DISPFUNCOUT();
  return info.GetReturnValue().Set(ARRAY_AT(a, v8v->ocv.v.vt));
}

NAN_METHOD(V8Variant::OLEBoolean)
{
  DISPFUNCIN();
  V8Variant *v8v = V8Variant::Unwrap<V8Variant>(info.This());
  CHECK_V8(V8Variant, v8v);
  if(v8v->ocv.v.vt != VT_BOOL)
    return Nan::ThrowTypeError("OLEBoolean source type OCVariant is not VT_BOOL");
  bool c_boolVal = v8v->ocv.v.boolVal != VARIANT_FALSE;
  DISPFUNCOUT();
  return info.GetReturnValue().Set(c_boolVal);
}

NAN_METHOD(V8Variant::OLEInt32)
{
  DISPFUNCIN();
  V8Variant *v8v = V8Variant::Unwrap<V8Variant>(info.This());
  CHECK_V8(V8Variant, v8v);
  VARIANT& v = v8v->ocv.v;
  if(v.vt != VT_I4 && v.vt != VT_INT
  && v.vt != VT_UI4 && v.vt != VT_UINT)
    return Nan::ThrowTypeError("OLEInt32 source type OCVariant is not VT_I4 nor VT_INT nor VT_UI4 nor VT_UINT");
  DISPFUNCOUT();
  return info.GetReturnValue().Set(Nan::New(v.lVal));
}

NAN_METHOD(V8Variant::OLENumber)
{
  DISPFUNCIN();
  V8Variant *v8v = V8Variant::Unwrap<V8Variant>(info.This());
  CHECK_V8(V8Variant, v8v);
  VARIANT& v = v8v->ocv.v;
  if(v.vt != VT_R8)
    return Nan::ThrowTypeError("OLENumber source type OCVariant is not VT_R8");
  DISPFUNCOUT();
  return info.GetReturnValue().Set(Nan::New(v.dblVal));
}

NAN_METHOD(V8Variant::OLEDate)
{
  DISPFUNCIN();
  V8Variant *v8v = V8Variant::Unwrap<V8Variant>(info.This());
  CHECK_V8(V8Variant, v8v);
  VARIANT& v = v8v->ocv.v;
  if(v.vt != VT_DATE)
    return Nan::ThrowTypeError("OLEDate source type OCVariant is not VT_DATE");
  Local<Date> result = OLEDateToObject(v.date);
  DISPFUNCOUT();
  return info.GetReturnValue().Set(result);
}

NAN_METHOD(V8Variant::OLEUtf8)
{
  DISPFUNCIN();
  V8Variant *v8v = V8Variant::Unwrap<V8Variant>(info.This());
  CHECK_V8(V8Variant, v8v);
  VARIANT& v = v8v->ocv.v;
  if(v.vt != VT_BSTR)
    return Nan::ThrowTypeError("OLEUtf8 source type OCVariant is not VT_BSTR");
  Handle<Value> result;
  if(!v.bstrVal) result = Nan::Undefined(); // or Null();
  else {
    std::wstring wstr(v.bstrVal);
    char *cs = wcs2u8s(wstr.c_str());
    result = Nan::New(cs).ToLocalChecked();
    free(cs);
  }
  DISPFUNCOUT();
  return info.GetReturnValue().Set(result);
}

NAN_METHOD(V8Variant::OLEValue)
{
  OLETRACEIN();
  OLETRACEVT(info.This());
  OLETRACEFLUSH();
  Local<Object> thisObject = info.This();
  OLETRACEVT(thisObject);
  OLETRACEFLUSH();
  V8Variant *v8v = V8Variant::Unwrap<V8Variant>(info.This());
  CHECK_V8(V8Variant, v8v);
  OLETRACEOUT();
  return info.GetReturnValue().Set(VariantToValue(v8v->ocv.v));
}

Local<Value> V8Variant::resolveValueChain(Local<Object> thisObject, const char* prop)
{
  OLETRACEIN();
  V8Variant *vThis = V8Variant::Unwrap<V8Variant>(thisObject);
  CHECK_V8_UNDEFINED(V8Variant, vThis);
  Local<Value> vResult = VariantToValue(vThis->ocv.v);
  if (vResult->IsUndefined()) return Nan::Undefined();
  vResult = INSTANCE_CALL(Nan::To<Object>(vResult).ToLocalChecked(), prop, 0, NULL);
  return vResult;
}

NAN_METHOD(V8Variant::OLEStringValue)
{
  OLETRACEIN();
  Local<Value> vResult = resolveValueChain(info.This(), "toString");
  if (vResult->IsUndefined()) return;
  OLETRACEOUT();
  return info.GetReturnValue().Set(vResult);
}

NAN_METHOD(V8Variant::OLELocaleStringValue)
{
  OLETRACEIN();
  Local<Value> vResult = resolveValueChain(info.This(), "toLocaleString");
  if (vResult->IsUndefined()) return;
  OLETRACEOUT();
  return info.GetReturnValue().Set(vResult);
}

Local<Date> V8Variant::OLEDateToObject(const DATE& dt)
{
  DISPFUNCIN();
  SYSTEMTIME syst;
  VariantTimeToSystemTime(dt, &syst);
  struct tm t = { 0 }; // set t.tm_isdst = 0
  t.tm_year = syst.wYear - 1900;
  t.tm_mon = syst.wMonth - 1;
  t.tm_mday = syst.wDay;
  t.tm_hour = syst.wHour;
  t.tm_min = syst.wMinute;
  t.tm_sec = syst.wSecond;
  DISPFUNCOUT();
  return Nan::New<Date>(mktime(&t) * 1000.0 + syst.wMilliseconds).ToLocalChecked();
}

Local<Value> V8Variant::ArrayPrimitiveToValue(void* loc, VARTYPE vt, unsigned cbElements, unsigned idx)
{
  /*
  *  VT_CY               [V][T][P][S]  currency
  *  VT_UNKNOWN          [V][T]   [S]  IUnknown *
  *  VT_DECIMAL          [V][T]   [S]  16 byte fixed point
  *  VT_RECORD           [V]   [P][S]  user defined type
  */
  switch (vt)
  {
  case VT_DISPATCH:
    // ASSERT: cbElements == sizeof(IDispatch*)
    if (reinterpret_cast<IDispatch**>(loc)[idx] == NULL) {
      return Nan::Null();
    } else {
      MaybeLocal<Object> mvReturn = V8Dispatch::CreateNew(reinterpret_cast<IDispatch**>(loc)[idx]);
      return mvReturn.IsEmpty() ? Local<Value>(Nan::Undefined()) : mvReturn.ToLocalChecked();
    }
  case VT_ERROR:
    // ASSERT: cbElements == sizeof(SCODE)
    return Exception::Error(Nan::New<String>((const uint16_t*)errorFromCodeW(HRESULT_FROM_WIN32(reinterpret_cast<SCODE*>(loc)[idx])).c_str()).ToLocalChecked());
  case VT_BOOL:
    // ASSERT: cbElements == sizeof(VARIANT_BOOL)
    return reinterpret_cast<VARIANT_BOOL*>(loc)[idx] != VARIANT_FALSE ? Nan::True() : Nan::False();
  case VT_I1:
    // ASSERT: cbElements == sizeof(CHAR)
    return Nan::New(reinterpret_cast<CHAR*>(loc)[idx]);
  case VT_UI1:
    // ASSERT: cbElements == sizeof(BYTE)
    return Nan::New(reinterpret_cast<BYTE*>(loc)[idx]);
  case VT_I2:
    // ASSERT: cbElements == sizeof(SHORT)
    return Nan::New(reinterpret_cast<SHORT*>(loc)[idx]);
  case VT_UI2:
    // ASSERT: cbElements == sizeof(USHORT)
    return Nan::New(reinterpret_cast<USHORT*>(loc)[idx]);
  case VT_I4:
    // ASSERT: cbElements == sizeof(LONG)
    return Nan::New(reinterpret_cast<LONG*>(loc)[idx]);
  case VT_UI4:
    // ASSERT: cbElements == sizeof(ULONG)
    return  Nan::New((uint32_t)reinterpret_cast<ULONG*>(loc)[idx]);
  case VT_INT:
    // ASSERT: cbElements == sizeof(INT)
    return Nan::New(reinterpret_cast<INT*>(loc)[idx]);
  case VT_UINT:
    // ASSERT: cbElements == sizeof(UINT)
    return Nan::New(reinterpret_cast<UINT*>(loc)[idx]);
  case VT_R4:
    // ASSERT: cbElements == sizeof(FLOAT)
    return Nan::New(reinterpret_cast<FLOAT*>(loc)[idx]);
  case VT_R8:
    // ASSERT: cbElements == sizeof(DOUBLE)
    return Nan::New(reinterpret_cast<DOUBLE*>(loc)[idx]);
  case VT_BSTR:
    // ASSERT: cbElements == sizeof(BSTR)
    if (!reinterpret_cast<BSTR*>(loc)[idx]) return Nan::Undefined(); // really shouldn't happen
    return Nan::New<String>((const uint16_t*)reinterpret_cast<BSTR*>(loc)[idx]).ToLocalChecked();
  case VT_DATE:
    // ASSERT: cbElements == sizeof(DATE)
    return OLEDateToObject(reinterpret_cast<DATE*>(loc)[idx]);
  case VT_VARIANT:
    // ASSERT: cbElements == sizeof(VARIANT)
    return VariantToValue(reinterpret_cast<VARIANT*>(loc)[idx]);
  default:
    {
      std::cerr << "[unknown type " << vt << " (not implemented now)]" << std::endl;
      std::cerr.flush();
    }
    return Nan::Undefined();
  }
}

Local<Value> V8Variant::ArrayToValue(const SAFEARRAY& a)
{
  OLETRACEIN();
  OLETRACEFLUSH();
  VARTYPE vt = VT_EMPTY;
  if (a.fFeatures&FADF_BSTR)
  {
    vt = VT_BSTR;
  }
  else if (a.fFeatures&FADF_UNKNOWN)
  {
    vt = VT_UNKNOWN;
  }
  else if (a.fFeatures&FADF_DISPATCH)
  {
    vt = VT_DISPATCH;
  }
  else if (a.fFeatures&FADF_VARIANT)
  {
    vt = VT_VARIANT;
  }
  else if (a.fFeatures&FADF_HAVEVARTYPE)
  {
    HRESULT hr = SafeArrayGetVartype(const_cast<SAFEARRAY*>(&a), &vt);
    if (FAILED(hr))
    {
      std::cerr << "[Unable to get type of array: " << errorFromCode(hr) << "]" << std::endl;
      std::cerr.flush();
      return Nan::Undefined();
    }
  }
  if (vt == VT_EMPTY)
  {
    std::cerr << "[Unable to get type of array (no useful flags set)]" << std::endl;
    std::cerr.flush();
    return Nan::Undefined();
  }
  const SAFEARRAYBOUND& bnds = a.rgsabound[0];
  if (a.cDims == 0)
  {
    return Nan::New<Array>(0);
  }
  else if (a.cDims == 1)
  {
    // fast array copy, using SafeArrayAccessData
    Local<Array> result = Nan::New<Array>(bnds.cElements);
    void* raw;
    HRESULT hr = SafeArrayAccessData(const_cast<SAFEARRAY*>(&a), &raw);
    if (FAILED(hr))
    {
      std::cerr << "[Unable to access array contents: " << errorFromCode(hr) << "]" << std::endl;
      std::cerr.flush();
      return Nan::Undefined();
    }
    for (unsigned idx = 0; idx < bnds.cElements; ++idx)
    {
      Nan::Set(result, idx, ArrayPrimitiveToValue(raw, vt, a.cbElements, idx));
    }
    hr = SafeArrayUnaccessData(const_cast<SAFEARRAY*>(&a));
    if (FAILED(hr))
    {
      std::cerr << "[Unable to release array contents: " << errorFromCode(hr) << "]" << std::endl;
      std::cerr.flush();
    }
    return result;
  } else {
    // slow array copy, using SafeArrayGetElement
    Local<Array> result = Nan::New<Array>(bnds.cElements);
    for (unsigned idx = 0; idx < bnds.cElements; ++idx)
    {
      LONG srcIndex = idx + bnds.lLbound;
      Nan::Set(result, idx, ArrayToValueSlow(a, vt, &srcIndex, 1));
    }
    return result;
  }
  OLETRACEOUT();
}

Local<Value> V8Variant::ArrayToValueSlow(const SAFEARRAY& a, VARTYPE vt, LONG* idices, unsigned numIdx)
{
  OLETRACEIN();
  OLETRACEFLUSH();
  if (numIdx <= a.cDims)
  {
    void* elm = alloca(a.cbElements);
    HRESULT hr = SafeArrayGetElement(const_cast<SAFEARRAY*>(&a), idices, elm);
    if (FAILED(hr))
    {
      std::cerr << "[Unable to access array element: " << errorFromCode(hr) << "]" << std::endl;
      std::cerr.flush();
      return Nan::Undefined();
    }
    return ArrayPrimitiveToValue(elm, vt, a.cbElements, 0);
  } else {
    LONG* newIdx = (LONG*)alloca(sizeof(LONG) * (numIdx + 1));
    memcpy(newIdx, idices, sizeof(LONG)*numIdx);
    const SAFEARRAYBOUND& bnds = a.rgsabound[numIdx-1];
    Local<Array> result = Nan::New<Array>(bnds.cElements);
    for (unsigned idx = 0; idx < bnds.cElements; ++idx)
    {
      newIdx[numIdx] = idx + bnds.lLbound;
      Nan::Set(result, idx, ArrayToValueSlow(a, vt, newIdx, numIdx+1));
    }
    return result;
  }
  OLETRACEOUT();
}

Local<Value> V8Variant::VariantToValue(const VARIANT& v)
{
  OLETRACEIN();
  OLETRACEFLUSH();
  /*
  *  VT_CY               [V][T][P][S]  currency
  *  VT_UNKNOWN          [V][T]   [S]  IUnknown *
  *  VT_DECIMAL          [V][T]   [S]  16 byte fixed point
  *  VT_RECORD           [V]   [P][S]  user defined type
  */
  Local<Object> result;
  switch (v.vt)
  {
  case VT_EMPTY:
  case VT_NULL:
    return Nan::Null();
  case VT_ERROR:
    return Exception::Error(Nan::New<String>((const uint16_t*)errorFromCodeW(HRESULT_FROM_WIN32(v.scode)).c_str()).ToLocalChecked());
  case VT_ERROR | VT_BYREF:
    if (!v.ppdispVal) return Nan::Undefined(); // really shouldn't happen
    return Exception::Error(Nan::New<String>((const uint16_t*)errorFromCodeW(HRESULT_FROM_WIN32(*v.pscode)).c_str()).ToLocalChecked());
  case VT_DISPATCH:
    if (v.pdispVal == NULL) {
      return Nan::Null();
    } else {
      MaybeLocal<Object> mvReturn = V8Dispatch::CreateNew(v.pdispVal);
      return mvReturn.IsEmpty() ? Local<Value>(Nan::Undefined()) : mvReturn.ToLocalChecked();
    }
  case VT_DISPATCH | VT_BYREF:
    if (!v.ppdispVal) return Nan::Undefined(); // really shouldn't happen
    if (*v.ppdispVal == NULL) {
      return Nan::Null();
    } else {
      MaybeLocal<Object> mvReturn = V8Dispatch::CreateNew(*v.ppdispVal);
      return mvReturn.IsEmpty() ? Local<Value>(Nan::Undefined()) : mvReturn.ToLocalChecked();
    }
  case VT_BOOL:
    return v.boolVal != VARIANT_FALSE ? Nan::True() : Nan::False();
  case VT_BOOL | VT_BYREF:
    if (!v.pboolVal) return Nan::Undefined(); // really shouldn't happen
    return *v.pboolVal != VARIANT_FALSE ? Nan::True() : Nan::False();
  case VT_I1:
    return Nan::New(v.cVal);
  case VT_I1 | VT_BYREF:
    if (!v.pcVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New(*v.pcVal);
  case VT_UI1:
    return Nan::New(v.bVal);
  case VT_UI1 | VT_BYREF:
    if (!v.pbVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New(*v.pbVal);
  case VT_I2:
    return Nan::New(v.iVal);
  case VT_I2 | VT_BYREF:
    if (!v.piVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New(*v.piVal);
  case VT_UI2:
    return Nan::New(v.uiVal);
  case VT_UI2 | VT_BYREF:
    if (!v.puiVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New(*v.puiVal);
  case VT_I4:
    return Nan::New(v.lVal);
  case VT_I4 | VT_BYREF:
    if (!v.plVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New(*v.plVal);
  case VT_UI4:
    return Nan::New((uint32_t)v.ulVal);
  case VT_UI4 | VT_BYREF:
    if (!v.pulVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New((uint32_t)*v.pulVal);
  case VT_INT:
    return Nan::New(v.intVal);
  case VT_INT | VT_BYREF:
    if (!v.pintVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New(*v.pintVal);
  case VT_UINT:
    return Nan::New(v.uintVal);
  case VT_UINT | VT_BYREF:
    if (!v.puintVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New(*v.puintVal);
  case VT_R4:
    return Nan::New(v.fltVal);
  case VT_R4 | VT_BYREF:
    if (!v.pfltVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New(*v.pfltVal);
  case VT_R8:
    return Nan::New(v.dblVal);
  case VT_R8 | VT_BYREF:
    if (!v.pdblVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New(*v.pdblVal);
  case VT_BSTR:
    if (!v.bstrVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New<String>((const uint16_t*)v.bstrVal).ToLocalChecked();
  case VT_BSTR | VT_BYREF:
    if (!v.pbstrVal || !*v.pbstrVal) return Nan::Undefined(); // really shouldn't happen
    return Nan::New<String>((const uint16_t*)*v.pbstrVal).ToLocalChecked();
  case VT_DATE:
    return OLEDateToObject(v.date);
  case VT_DATE | VT_BYREF:
    if (!v.pdate) return Nan::Undefined(); // really shouldn't happen
    return OLEDateToObject(*v.pdate);
  case VT_VARIANT | VT_BYREF:
    if (!v.pvarVal) return Nan::Undefined(); // really shouldn't happen
    return VariantToValue(*v.pvarVal);
  case VT_SAFEARRAY:
    if (!v.parray) return Nan::Undefined(); // really shouldn't happen
    return ArrayToValue(*v.parray);
  case VT_SAFEARRAY | VT_BYREF:
    if (!v.pparray || !*v.pparray) return Nan::Undefined(); // really shouldn't happen
    return ArrayToValue(**v.pparray);
  default:
    if (v.vt & VT_ARRAY)
    {
      if (v.vt & VT_BYREF)
      {
        if (!v.pparray || !*v.pparray) return Nan::Undefined(); // really shouldn't happen
        return ArrayToValue(**v.pparray);
      } else {
        if (!v.parray) return Nan::Undefined(); // really shouldn't happen
        return ArrayToValue(*v.parray);
      }
    } else {
      // we don't know how to handle this type, wrap it with a V8Variant
      MaybeLocal<Object> mvResult = V8Variant::CreateUndefined();
      if(mvResult.IsEmpty()) return Nan::Undefined();
      Local<Object> vResult = mvResult.ToLocalChecked();
      V8Variant *o = V8Variant::Unwrap<V8Variant>(vResult);
      CHECK_V8_UNDEFINED(V8Variant, o);
      VariantCopy(&o->ocv.v, &v); // copy rv value
      return vResult;
    }
  }
  OLETRACEOUT();
}

static std::string GetName(ITypeInfo *typeinfo, MEMBERID id) {
  BSTR name;
  UINT numNames = 0;
  typeinfo->GetNames(id, &name, 1, &numNames);
  if (numNames > 0) {
    return BSTR2MBCS(name);
  }
  return "";
}

MaybeLocal<Object> V8Variant::CreateUndefined(void)
{
  DISPFUNCIN();
  Local<FunctionTemplate> localClazz = Nan::New(clazz);
  return Nan::NewInstance(Nan::GetFunction(localClazz).ToLocalChecked(), 0, NULL);
  DISPFUNCOUT();
}

NAN_METHOD(V8Variant::New)
{
  DISPFUNCIN();
  if(!info.IsConstructCall())
    return Nan::ThrowTypeError("Use the new operator to create new V8Variant objects");
  Local<Object> thisObject = info.This();
  V8Variant *v = new V8Variant(); // must catch exception
  CHECK_V8(V8Variant, v);
  v->Wrap(thisObject); // InternalField[0]
  DISPFUNCOUT();
  return info.GetReturnValue().Set(thisObject);
}

NAN_METHOD(V8Variant::Finalize)
{
  DISPFUNCIN();
#if(0)
  std::cerr << __FUNCTION__ << " Finalizer is called\a" << std::endl;
  std::cerr.flush();
#endif
  V8Variant *v = V8Variant::Unwrap<V8Variant>(info.This());
  if(v) v->Finalize();
  DISPFUNCOUT();
}

void V8Variant::Finalize()
{
  if(!finalized)
  {
    ocv.Clear();
    finalized = true;
  }
}

} // namespace node_win32ole
