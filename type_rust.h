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

#ifndef AIDL_TYPE_RUST_H_
#define AIDL_TYPE_RUST_H_

#include <memory>
#include <string>
#include <set>
#include <vector>

#include <android-base/macros.h>

#include "ast_rust.h"
#include "type_namespace.h"

namespace android {
namespace aidl {
namespace rust {

class Type : public ValidatableType {
 public:
  Type(int kind,  // from ValidatableType
       const std::string& package,
       const std::string& aidl_type,
       const std::string& rust_owned_type,
       const std::string& rust_borrow_type_,
       Type *array_type = nullptr,
       const std::string& src_file_name = "",
       int line = -1);
  virtual ~Type() = default;

  // overrides of ValidatableType
  bool CanBeOutParameter() const override { return false; }
  bool CanWriteToParcel() const override;

  std::string RustOwnedType() const { return rust_type_; }
  std::string RustBorrowedType() const { return rust_borrow_type_; }
  // An Expr returning an instance of rust_type_.
  // You may use Try() to return std::io::Error.
  // TODO: Parcel should be a unique_ptr<Expr>, but this makes it impossible to
  // use in multiple methods. So either it needs to be Shared, or it needs to
  // get a copy constructor.
  virtual std::unique_ptr<Expr> ReadFromParcel(std::string parcel_path) const = 0;
  // An Expr that takes care of writing an instance of rust_type_, and returns
  // ()
  // TODO: same as above.
  virtual std::unique_ptr<Expr> WriteToParcel(std::string parcel_path, std::unique_ptr<Expr> args) const = 0;

  const Type* ArrayType() const override { return array_type_.get(); }
  const Type* NullableType() const override { return nullptr; }

  virtual bool IsRustPrimitive() const { return false; }
  /*virtual std::string WriteCast(const std::string& value) const {
    return value;
  }*/

 private:
  // |rust_type| is what we use in the generated Rust code (e.g. "i32") - e.g.
  // what the Expr from ReadFromParcel returns.
  const std::string rust_type_;
  const std::string rust_borrow_type_;

  const std::unique_ptr<Type> array_type_;
  //const std::unique_ptr<Type> nullable_type_;

  DISALLOW_COPY_AND_ASSIGN(Type);
};  // class Type

class TypeNamespace : public ::android::aidl::LanguageTypeNamespace<Type> {
 public:
  TypeNamespace() = default;
  virtual ~TypeNamespace() = default;

  void Init() override;
  bool AddParcelableType(const AidlParcelable& p,
                         const std::string& filename) override;
  bool AddBinderType(const AidlInterface& b,
                     const std::string& filename) override;
  bool AddListType(const std::string& type_name) override;
  bool AddMapType(const std::string& key_type_name,
                  const std::string& value_type_name) override;

  bool IsValidPackage(const std::string& package) const override;
  const ValidatableType* GetArgType(const AidlArgument& a,
                             int arg_index,
                             const std::string& filename,
                             const AidlInterface& interface) const override;

  const Type* VoidType() const { return void_type_; }
  //const Type* IBinderType() const { return ibinder_type_; }

 private:
  Type* void_type_ = nullptr;
  Type* string_type_ = nullptr;
  Type* ibinder_type_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(TypeNamespace);
};  // class TypeNamespace

}  // namespace rust
}  // namespace aidl
}  // namespace android

#endif  // AIDL_TYPE_NAMESPACE_RUST_H_
