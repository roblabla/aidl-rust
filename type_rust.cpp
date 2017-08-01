/*
 * Copyright (C) 2015, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "type_rust.h"

#include <algorithm>
#include <iostream>
#include <vector>

#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "logging.h"

using std::cerr;
using std::endl;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;

using android::base::Split;
using android::base::Join;
using android::base::StringPrintf;

namespace android {
namespace aidl {
namespace rust {
namespace {

const char kNoPackage[] = "";
const char kNoValidMethod[] = "";
Type* const kNoArrayType = nullptr;

/*bool is_rust_keyword(const std::string& str) {
  static const std::vector<std::string> kRustKeywords{

    "as", "box", "break", "const", "continue", "crate", "else", "enum",
    "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match",
    "mod", "move", "mut", "pub", "ref", "return", "self", "Self", "static",
    "struct", "super", "trait", "true", "type", "unsafe", "use", "where",
    "while", "abstract", "alignof", "become", "do", "final", "macro",
    "offsetof", "override", "priv", "proc", "pure", "sizeof", "typeof",
    "unsized", "virtual", "yield"
  };
  return std::find(kRustKeywords.begin(), kRustKeywords.end(), str) !=
      kRustKeywords.end();
}*/

} // namespace

Type::Type(int kind,
           const std::string& package,
           const std::string& aidl_type,
           const string& rust_type,
           const string& rust_borrow_type,
           Type *array_type,
           const string& src_file_name,
           int line)
    : ValidatableType(kind, package, aidl_type, src_file_name, line),
      rust_type_(rust_type),
      rust_borrow_type_(rust_borrow_type),
      array_type_(array_type) {}

bool Type::CanWriteToParcel() const { return true; }

// Type that can be simply read/written to a Parcel through a method like
// data.read_method();
// data.write_method(to_write);
class SimpleType : public Type {
 public:
  SimpleType(int kind,
             const std::string& package,
             const std::string& aidl_type,
             const std::string& rust_type,
             const std::string& rust_borrowed_type,
             const std::string& read_method,
             const std::string& write_method,
             Type *array_type = nullptr,
             const std::string& src_file_name = "",
             int line = -1)
    : Type(kind, package, aidl_type, rust_type, rust_borrowed_type, array_type,
           src_file_name, line),
      read_method_(read_method), write_method_(write_method) {}

  std::unique_ptr<Expr> ReadFromParcel(std::string parcel_path) const override {
    vector<unique_ptr<Expr>> args;
    args.emplace_back(new Path(parcel_path));
    return std::unique_ptr<Expr>(new MethodCall(read_method_, std::move(args)));
  }
  std::unique_ptr<Expr> WriteToParcel(std::string parcel_path, std::unique_ptr<Expr> to_write) const override {
    vector<unique_ptr<Expr>> args;
    args.emplace_back(new Path(parcel_path));
    args.push_back(std::move(to_write));
    return std::unique_ptr<Expr>(new MethodCall(write_method_, std::move(args)));
  }

 private:
  std::string read_method_;
  std::string write_method_;
}; // class SimpleType

class VoidType : public SimpleType {
 public:
  VoidType() : SimpleType(ValidatableType::KIND_BUILT_IN, kNoPackage, "void",
                    "()", "()", kNoValidMethod, kNoValidMethod) {}
  virtual ~VoidType() = default;
  bool CanBeOutParameter() const override { return false; }
  bool CanWriteToParcel() const override { return false; }
};  // class VoidType

class VecType : public SimpleType {
 public:
  VecType(int kind,
          const string& package,
          const string& underlying_aidl_type,
          const string& underlying_rust_type,
          const string& read_method,
          const string& write_method,
          const string& src_file_name = "")
      : SimpleType(kind, package,
             underlying_aidl_type + "[]",
             GetRustOwnedType(underlying_rust_type),
             GetRustBorrowedType(underlying_rust_type),
             read_method, write_method, kNoArrayType, src_file_name) {}

  bool CanBeOutParameter() const override { return true; }

 private:
  static string GetRustBorrowedType(const string& underlying_rust_type) {
    return StringPrintf("Option<&[%s]>",
                underlying_rust_type.c_str());
  }
  static string GetRustOwnedType(const string& underlying_rust_type) {
    return StringPrintf("Option<Vec<%s>>",
                underlying_rust_type.c_str());
  }

  DISALLOW_COPY_AND_ASSIGN(VecType);
}; // class VecType


class PrimitiveType : public SimpleType {
 public:
  PrimitiveType(const std::string& aidl_type,
                const std::string& rust_type,
                const std::string& read_method,
                const std::string& write_method,
                const std::string& read_array_method,
                const std::string& write_array_method)
      : SimpleType(ValidatableType::KIND_BUILT_IN, kNoPackage, aidl_type,
              rust_type, rust_type,
              read_method, write_method,
              new VecType(ValidatableType::KIND_BUILT_IN, kNoPackage,
                          aidl_type, rust_type,
                          read_array_method, write_array_method)) {}

  virtual ~PrimitiveType() = default;
  bool IsRustPrimitive() const override { return true; }

 private:
  DISALLOW_COPY_AND_ASSIGN(PrimitiveType);
}; // class PrimitiveType

class BinderType : public SimpleType {
 public:
  BinderType(const AidlInterface& interface, const std::string& src_file_name)
      : SimpleType(ValidatableType::KIND_GENERATED,
             interface.GetPackage(), interface.GetName(),
             GetRustName(interface), GetRustName(interface),
             "read_strong_binder", "write_strong_binder",
             kNoArrayType, src_file_name, interface.GetLine()) {}
  virtual ~BinderType() = default;

 private:
  static string GetRustName(const AidlInterface& interface) {
    return "::android::sp<" + GetRawRustName(interface) + ">";
  }

  static string GetRawRustName(const AidlInterface& interface) {
    vector<string> name = interface.GetSplitPackage();
    string ret;

    name.push_back(interface.GetName());

    for (const auto& term : name) {
      ret += "_" + term;
    }

    return ret;
  }
};

class ParcelableType : public Type {
 public:
  ParcelableType(const AidlParcelable& parcelable,
                 const std::string& src_file_name)
      : Type(ValidatableType::KIND_PARCELABLE,
             parcelable.GetPackage(), parcelable.GetName(),
             // TODO: Every parcelable is actually optional. This seems to be
             // basically "How aidl works". I'd need to check.
             StringPrintf("Option<%s>", GetRustName(parcelable).c_str()),
             StringPrintf("Option<&%s>", GetRustName(parcelable).c_str()),
             new VecType(ValidatableType::KIND_PARCELABLE, parcelable.GetPackage(),
                         parcelable.GetName(),
                         StringPrintf("Option<%s>", GetRustName(parcelable).c_str()),
                         "read_typed_array", "write_typed_array",
                         src_file_name),
             src_file_name, parcelable.GetLine()), inner_rust_type_(GetRustName(parcelable)) {}
  virtual ~ParcelableType() = default;
  bool CanBeOutParameter() const override { return true; }

  std::unique_ptr<Expr> ReadFromParcel(std::string parcel_path) const override {
    // if data.read_bool()? {
    //   Some(ParcelableType::read_from_parcel(&mut data)?)
    // } else {
    //   None
    // }
    vector<unique_ptr<Expr>> args;
    args.emplace_back(new Path(parcel_path));
    unique_ptr<If> if_expr (new If(new Try(new MethodCall("read_bool", std::move(args)))));

    Block *if_block = if_expr->GetIfBlock();
    vector<unique_ptr<Expr>> args_some;
    {
      vector<unique_ptr<Expr>> args;
      args.emplace_back(new Path(parcel_path));
      args_some.emplace_back(new Call(inner_rust_type_ + "::read_from_parcel", std::move(args)));
    }
    if_block->SetRet(new Call("Some", std::move(args_some)));

    Block *else_block = new Block(false);
    else_block->SetRet(new Path("None"));
    if_expr->SetElse(std::move(else_block));
    return std::move(if_expr);
  }
  std::unique_ptr<Expr> WriteToParcel(std::string parcel_path, std::unique_ptr<Expr> to_write) const override {
    // match to_write {
    //     Some(x) => {
    //         parcel.write_bool(true);
    //         x.write_to_parcel(parcel);
    //     },
    //     None => parcel.write_bool(false);
    // }
    unique_ptr<Match> ret { new Match(std::move(to_write)) };

    unique_ptr<Block> some_block {new Block(false)};
    {
      vector<unique_ptr<Expr>> args;
      args.emplace_back(new Path(parcel_path));
      args.emplace_back(new Lit("true"));
      some_block->AddStmt(new StmtExpr(new MethodCall("write_bool", std::move(args))));
    }
    {
      vector<unique_ptr<Expr>> args;
      args.emplace_back(new Path("x"));
      args.emplace_back(new Path(parcel_path));
      some_block->AddStmt(new StmtExpr(new MethodCall("write_to_parcel", std::move(args))));
    }
    ret->AddCase(new Arm("Some(x)", std::move(some_block)));
    {
      vector<unique_ptr<Expr>> args;
      args.emplace_back(new Path(parcel_path));
      args.emplace_back(new Lit("false"));
      ret->AddCase(new Arm("None", unique_ptr<Expr>(new MethodCall("write_bool", std::move(args)))));
    }
    return unique_ptr<Expr>(ret.release());
  }

 private:
  static string GetRustName(const AidlParcelable& parcelable) {
    return "::" + Join(parcelable.GetSplitPackage(), "::") +
        "::" + parcelable.GetCppName(); // Ugly, I know. I'm a lazy ass
  }

  string inner_rust_type_;
};

void TypeNamespace::Init() {
  // Let's just treat every byte as an u8.
  //Add(new ByteType());
  Add(new PrimitiveType(
      "byte", "u8", "read_u8", "write_u8", "read_u8_array", "write_u8_array"));
  Add(new PrimitiveType(
      "int", "i32", "read_i32", "write_i32", "read_i32_array", "write_i32_array"));
  Add(new PrimitiveType(
      "long", "i64", "read_i64", "write_i64", "read_i64_array", "write_i64_array"));
  Add(new PrimitiveType(
      "float", "f32", "read_f32", "write_f32", "read_f32_array", "write_f32_array"));
  Add(new PrimitiveType(
      "double", "f64", "read_f64", "write_f64", "read_f64_array", "write_f64_array"));
  Add(new PrimitiveType(
      "boolean", "bool", "read_bool", "write_bool", "read_bool_array", "write_bool_array"));
  // Rust doesn't have an utf16 char type.
  /*Add(new PrimitiveType(
      "char",
      kNoHeader, "char16_t", "readChar", "writeChar",
      "readCharVector", "writeCharVector"));*/

  Type* string_array_type = new VecType(
      ValidatableType::KIND_BUILT_IN, "java.lang", "String",
      "Option<String>", "read_string16_array", "write_string16_array");

  /*Type* nullable_string_type =
      new Type(ValidatableType::KIND_BUILT_IN, "java.lang", "String",
               {"memory", "utils/String16.h"}, "::std::unique_ptr<::android::String16>",
               "readString16", "writeString16");

  */string_type_ = new SimpleType(ValidatableType::KIND_BUILT_IN, "java.lang", "String",
                          "Option<String>", "Option<&str>",
                          "read_string16", "write_string16",
                          string_array_type/*, nullable_string_type*/);
  Add(string_type_);
/*
  using ::android::aidl::kAidlReservedTypePackage;
  using ::android::aidl::kUtf8InCppStringClass;

  Type* nullable_ibinder = new Type(
      ValidatableType::KIND_BUILT_IN, "android.os", "IBinder",
      {"binder/IBinder.h"}, "::android::sp<::android::IBinder>",
      "readNullableStrongBinder", "writeStrongBinder");
  */
  ibinder_type_ = new SimpleType(
      ValidatableType::KIND_BUILT_IN, "android.os", "IBinder",
      "::std::rc::Rc<::std::cell::RefCell<::binder::Handle>>", "::std::rc::Rc<::std::cell::RefCell<::binder::Handle>>",
      "read_strong_binder", "write_strong_binder");
  Add(ibinder_type_);

  /*Add(new BinderListType());
  Add(new StringListType());
  Add(new Utf8InCppStringListType());

  Type* fd_vector_type = new CppArrayType(
      ValidatableType::KIND_BUILT_IN, kNoPackage, "FileDescriptor",
      "android-base/unique_fd.h",
      "::android::base::unique_fd",
      "readUniqueFileDescriptorVector", "writeUniqueFileDescriptorVector",
      false);

  Add(new Type(
      ValidatableType::KIND_BUILT_IN, kNoPackage, "FileDescriptor",
      {"android-base/unique_fd.h"}, "::android::base::unique_fd",
      "readUniqueFileDescriptor", "writeUniqueFileDescriptor",
      fd_vector_type));
*/
  void_type_ = new class VoidType();
  Add(void_type_);
}

bool TypeNamespace::AddParcelableType(const AidlParcelable& p,
                                      const string& filename) {
  Add(new ParcelableType(p, filename));
  return true;
}

bool TypeNamespace::AddBinderType(const AidlInterface& b,
                                  const string& file_name) {
  Add(new BinderType(b, file_name));
  return true;
}

bool TypeNamespace::AddListType(const std::string& type_name) {
  LOG(ERROR) << "aidl-rust does not yet support List<" << type_name << ">";
  return false;
}

bool TypeNamespace::AddMapType(const std::string& /* key_type_name */,
                               const std::string& /* value_type_name */) {
  // TODO Support list types b/25242025
  LOG(ERROR) << "aidl-rust does not yet support typed maps!";
  return false;
}

bool TypeNamespace::IsValidPackage(const string& package) const {
  /*if (package.empty()) {
    return false;
  }

  auto pieces = Split(package, ".");
  for (const string& piece : pieces) {
    if (is_cpp_keyword(piece)) {
      return false;
    }
  }*/

  return true;
}

const ValidatableType* TypeNamespace::GetArgType(const AidlArgument& a,
    int arg_index,
    const std::string& filename,
    const AidlInterface& interface) const {
  const string error_prefix = StringPrintf(
      "In file %s line %d parameter %s (%d):\n    ",
      filename.c_str(), a.GetLine(), a.GetName().c_str(), arg_index);

  // check that the name doesn't match a keyword
  /*if (is_cpp_keyword(a.GetName().c_str())) {
    cerr << error_prefix << "Argument name is a Rust keyword"
         << endl;
    return nullptr;
  }*/

  return ::android::aidl::TypeNamespace::GetArgType(a, arg_index, filename,
                                                    interface);
}

}  // namespace cpp
}  // namespace aidl
}  // namespace android
