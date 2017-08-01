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

#include "ast_rust.h"

#include <algorithm>
#include <iostream>
#include "code_writer.h"
#include "logging.h"

using std::string;
using std::unique_ptr;
using std::vector;

namespace android {
namespace aidl {
namespace rust {

StmtExpr::StmtExpr(Expr* expr)
  : expr_(expr) {}

StmtExpr::StmtExpr(unique_ptr<Expr> expr)
  : expr_(std::move(expr)) {}

void StmtExpr::Write(CodeWriter* to) const {
  expr_->Write(to);
  if (expr_->RequiresSemiToBeStmt()) {
    to->Write(";");
  }
}

Local::Local(Modifiers mod, const std::string& name, std::unique_ptr<Expr> init)
  : mod_(mod), name_(name), ty_(nullptr), init_(std::move(init)) {}

Local::Local(Modifiers mod, const std::string& name, const std::string& ty, std::unique_ptr<Expr> init)
  : mod_(mod), name_(name), ty_(new string(ty)), init_(std::move(init)) {}

void Local::Write(CodeWriter* to) const {
  to->Write("let ");
  if (mod_ == BYVAL_MUT) {
    to->Write("mut ");
  }
  to->Write("%s", name_.c_str());
  if (ty_ != nullptr) {
    to->Write(" : %s", ty_->c_str());
  }
  if (init_ != nullptr) {
    to->Write(" = ");
    init_->Write(to);
  }
  to->Write(";");
}

Lit::Lit(const std::string& lit)
  : lit_(lit) {}

void Lit::Write(CodeWriter* to) const {
  to->Write("%s", lit_.c_str());
}

AddrOf::AddrOf(bool mut, Expr* expr)
  : mut_(mut), expr_(expr) {}

AddrOf::AddrOf(bool mut, unique_ptr<Expr> expr)
  : mut_(mut), expr_(std::move(expr)) {}

void AddrOf::Write(CodeWriter* to) const {
  to->Write("&");
  if (mut_) {
    to->Write("mut ");
  }
  expr_->Write(to);
}

Cast::Cast(Expr* expr, const std::string& ty)
  : expr_(expr), ty_(ty) {}

void Cast::Write(CodeWriter* to) const {
  expr_->Write(to);
  to->Write(" as %s", ty_.c_str());
}

BinOp::BinOp(const std::string& op, Expr* left, Expr* right)
  : op_(op), left_(left), right_(right) {}

void BinOp::Write(CodeWriter* to) const {
  left_->Write(to);
  to->Write(" %s ", op_.c_str());
  right_->Write(to);
}

Path::Path(const std::string& ident)
  : name_(ident) {}

void Path::Write(CodeWriter* to) const {
  to->Write("%s", name_.c_str());
}

Try::Try(Expr *expr)
  : expr_(expr) {}

Try::Try(unique_ptr<Expr> expr)
  : expr_(std::move(expr)) {}

void Try::Write(CodeWriter* to) const {
  expr_->Write(to);
  to->Write("?");
}

Call::Call(const string& fn, vector<unique_ptr<Expr>> args)
  : fn_(new Path(fn)), args_(std::move(args)) {}

Call::Call(Expr* fn, vector<unique_ptr<Expr>> args)
  : fn_(fn), args_(std::move(args)) {}

void Call::Write(CodeWriter* to) const {
  fn_->Write(to);
  to->Write("(");
  for (const auto& arg: args_) {
    arg->Write(to);
    to->Write(",");
  }
  to->Write(")");
}

Field::Field(Expr* on, const std::string& name)
  : on_(on), name_(name) {}

void Field::Write(CodeWriter* to) const {
  on_->Write(to);
  to->Write(".%s", name_.c_str());
}

MethodCall::MethodCall(const std::string& name, vector<unique_ptr<Expr>> args)
  : name_(name), args_(std::move(args)) {}

MethodCall::MethodCall(const std::string& name, Expr* args)
  : name_(name) {
  args_.emplace_back(args);
}

void MethodCall::Write(CodeWriter* to) const {
  // TODO: Bounds checking
  args_[0]->Write(to);
  to->Write(".%s(", name_.c_str());
  std::for_each(args_.begin() + 1, args_.end(), [&](const unique_ptr<Expr> &i) {
    i->Write(to);
    to->Write(",");
  });
  to->Write(")");
}

Block::Block(bool unsafe)
    : unsafe_(unsafe) {}

void Block::AddStmt(Stmt* stmt) {
  stmts_.emplace_back(stmt);
}

void Block::SetRet(Expr* expr) {
  ret_ = unique_ptr<Expr>(expr);
}

void Block::Write(CodeWriter *to) const {
  if (unsafe_) {
    to->Write("unsafe ");
  }
  to->Write("{\n");
  for (const auto& stmt : stmts_) {
    stmt->Write(to);
    to->Write("\n");
  }
  if (ret_) {
    ret_->Write(to);
    to->Write("\n");
  }
  to->Write("}");
}

If::If(Expr* condition)
  : condition_(condition),
    if_(false),
    else_(nullptr) {}

Block* If::GetIfBlock() {
  return &if_;
}

Arm::Arm(const string& pat, unique_ptr<Expr> expr)
  : pat_(pat), expr_(std::move(expr)) {}

void Arm::Write(CodeWriter* to) const {
  to->Write("%s => ", pat_.c_str());
  expr_->Write(to);
}

Match::Match(std::unique_ptr<Expr> val)
  : val_(std::move(val)) {}

void Match::AddCase(Arm* case_) {
  cases_.emplace_back(case_);
}

void Match::Write(CodeWriter* to) const {
  to->Write("match ");
  val_->Write(to);
  to->Write(" {\n");
  for (const auto& case_ : cases_) {
    case_->Write(to);
    to->Write(",\n");
  }
  to->Write("}");
}

void If::SetElse(Expr* expr) {
  else_ = unique_ptr<Expr>(expr);
}

void If::Write(CodeWriter* to) const {
  to->Write("if ");
  condition_->Write(to);
  to->Write(" ");
  if_.Write(to);
  if (else_) {
    to->Write(" else ");
    else_->Write(to);
  }
}

FieldCtr::FieldCtr(const std::string& field_name, Expr* expr)
  : field_name_(field_name), expr_(expr) {}

void FieldCtr::Write(CodeWriter* to) const {
  to->Write("%s: ", field_name_.c_str());
  expr_->Write(to);
  to->Write(",");
}

StructCtr::StructCtr(const std::string& struct_name, vector<unique_ptr<FieldCtr>> fields)
  : struct_name_(struct_name), fields_(std::move(fields)) {}

void StructCtr::Write(CodeWriter* to) const {
  to->Write("%s {\n", struct_name_.c_str());
  for (const auto& field : fields_) {
    field->Write(to);
    to->Write("\n");
  }
  to->Write("}");
}

Item::Item(vector<string> attrs, bool pub)
  : attrs_(attrs), public_(pub) {}

void Item::Write(CodeWriter* to) const {
  for (const auto& attr : attrs_) {
    to->Write("#[%s]\n", attr.c_str());
  }
  if (public_) {
    to->Write("pub ");
  }
}

void Item::AddAttr(const std::string& attr) {
  attrs_.push_back(attr);
}

StructField::StructField(const std::string& name, const std::string& ty)
    : name_(name),
      ty_(ty) {}

void StructField::Write(CodeWriter* to) const {
  to->Write("pub %s: %s,", name_.c_str(), ty_.c_str());
}

Struct::Struct(bool pub, const std::string& name)
  : Item(vector<string>(), pub),
    name_(name) {}

void Struct::AddField(StructField* field) {
  members_.emplace_back(field);
}

void Struct::Write(CodeWriter* to) const {
  Item::Write(to);
  to->Write("struct %s {\n", name_.c_str());
  for (const auto& field : members_) {
    field->Write(to);
    to->Write("\n");
  }
  to->Write("}\n");
}

EnumVariant::EnumVariant(const std::string& name)
  : name_(name), discr_(nullptr) {}

EnumVariant::EnumVariant(const std::string& name, Expr* discr)
  : name_(name), discr_(discr) {}

void EnumVariant::Write(CodeWriter* to) const {
  to->Write("%s", name_.c_str());
  if (discr_ != nullptr) {
    to->Write(" = ");
    discr_->Write(to);
  }
  to->Write(",");
}

Enum::Enum(bool pub, const std::string& name)
  : Item(vector<string>(), pub),
    name_(name) {}

void Enum::AddVariant(EnumVariant* variant) {
  variants_.emplace_back(variant);
}

void Enum::Write(CodeWriter* to) const {
  Item::Write(to);
  to->Write("enum %s {\n", name_.c_str());
  for (const auto& var : variants_) {
    var->Write(to);
    to->Write("\n");
  }
  to->Write("}\n");
}

ImplItem::ImplItem(const std::string& name)
    : name_(name) {}

string ImplItem::GetName() const {
  return name_;
}

Arg::Arg(const std::string& name)
    : name_(name), infered_self_(true), type_("") {}

Arg::Arg(const std::string& name, const std::string& type)
    : name_(name), infered_self_(false), type_(type) {}

void Arg::Write(CodeWriter* to) const {
  to->Write("%s", name_.c_str());
  if (!infered_self_) {
    to->Write(": %s", type_.c_str());
  }
  to->Write(",");
}

MethodImpl::MethodImpl(const std::string& name,
           std::vector<std::unique_ptr<Arg>> args, const std::string& ret)
    : ImplItem(name), args_(std::move(args)), ret_type_(ret),
      block_(false) {}

void MethodImpl::Write(CodeWriter* to) const {
  to->Write("fn %s(", GetName().c_str());
  for (const auto& arg : args_) {
    arg->Write(to);
  }
  to->Write(") -> %s ", ret_type_.c_str());
  block_.Write(to);
}

Block* MethodImpl::GetBlock() {
  return &block_;
}

Use::Use(const std::string& path)
  : Item(vector<string>(), false), path_(path) {}

void Use::Write(CodeWriter* to) const {
  to->Write("use %s;\n", path_.c_str());
}

Impl::Impl(const std::string& on)
  : Item(vector<string>(), false), on_(on), trait_(nullptr) {}
Impl::Impl(const std::string& on, const std::string& trait)
  : Item(vector<string>(), false),
    on_(on),
    trait_(new string(trait)) {}

void Impl::Write(CodeWriter* to) const {
  Item::Write(to);
  if (trait_ != nullptr) {
    to->Write("impl %s for %s {\n", trait_->c_str(), on_.c_str());
  } else {
    to->Write("impl %s {\n", on_.c_str());
  }
  for (const auto& item : items_) {
    item->Write(to);
    to->Write("\n");
  }
  to->Write("}\n");
}

void Impl::AddItem(std::unique_ptr<ImplItem> item) {
  items_.push_back(std::move(item));
}

Document::Document(vector<unique_ptr<Item>> items)
  : items_(std::move(items)) {}

void Document::Write(CodeWriter* to) const {
  for (const auto& item : items_) {
    item->Write(to);
  }
}

}  // namespace rust
}  // namespace aidl
}  // namespace android

