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

#ifndef AIDL_AST_RUST_H_
#define AIDL_AST_RUST_H_

#include <memory>
#include <string>
#include <vector>

#include <android-base/macros.h>

namespace android {
namespace aidl {
class CodeWriter;
}  // namespace aidl
}  // namespace android

namespace android {
namespace aidl {
namespace rust {

// This is heavily based on syntex_syntax to make the Nodes as close as possible
// to the official rust AST.

class AstNode {
 public:
  AstNode() = default;
  virtual ~AstNode() = default;
  virtual void Write(CodeWriter* to) const = 0;
};  // class AstNode

// A statement is anything that can be put inside a block.
class Stmt : public AstNode {
 public:
   Stmt() = default;
   virtual ~Stmt() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(Stmt);
}; // class Stmt

// An expression is anything that returns a value.
class Expr : public AstNode {
 public:
  Expr() = default;
  virtual ~Expr() = default;

  virtual bool RequiresSemiToBeStmt() const { return true; };
 private:
  DISALLOW_COPY_AND_ASSIGN(Expr);
}; //class Expr

// I need this to stay type-safe... Basically, controls the ;
class StmtExpr : public Stmt {
 public:
  StmtExpr(Expr* expr);
  StmtExpr(std::unique_ptr<Expr> expr);

  void Write(CodeWriter* to) const override;

 private:
  std::unique_ptr<Expr> expr_;
  DISALLOW_COPY_AND_ASSIGN(StmtExpr);
}; // StmtExpr

// A let binding.
// TODO: Should take a pattern (instead of modifiers and a name)
class Local : public Stmt {
 public:
  enum Modifiers {
    BYVAL,
    BYVAL_MUT
  };
  Local(Modifiers mod, const std::string& name, std::unique_ptr<Expr> init);
  Local(Modifiers mod, const std::string& name, const std::string& ty, std::unique_ptr<Expr> init);

  void Write(CodeWriter* to) const override;
 private:
  Modifiers mod_;
  std::string name_;
  // Potentially nullptr if we don't specify type
  std::unique_ptr<std::string> ty_;
  // Potentially nullptr if this is just a let x;
  std::unique_ptr<Expr> init_;

  DISALLOW_COPY_AND_ASSIGN(Local);
}; // class Local

// A literal value (int, string, etc...).
// TODO: It'd be better to have IntLit, etc... but this will do for now.
class Lit : public Expr {
 public:
  Lit(const std::string& lit);

  void Write(CodeWriter* to) const override;
 private:
  std::string lit_;
  DISALLOW_COPY_AND_ASSIGN(Lit);
}; // class Lit

class AddrOf : public Expr {
 public:
  AddrOf(bool mut, Expr* expr);
  AddrOf(bool mut, std::unique_ptr<Expr> expr);

  void Write(CodeWriter* to) const override;
 private:
  bool mut_;
  std::unique_ptr<Expr> expr_;
  DISALLOW_COPY_AND_ASSIGN(AddrOf);
}; // class AddrOf

// Casts an expression to another type
// expr as ty
class Cast : public Expr {
 public:
  Cast(Expr* expr, const std::string& ty);

  void Write(CodeWriter* to) const override;

 private:
  std::unique_ptr<Expr> expr_;
  std::string ty_;
  DISALLOW_COPY_AND_ASSIGN(Cast);
}; // Cast

// A binary operation.
// TODO: It'd be better to have AddOp or an enum of ops to further type-check
// this, but it will do for now.
class BinOp : public Expr {
 public:
  BinOp(const std::string& op, Expr* left, Expr* right);

  void Write(CodeWriter* to) const override;
 private:
  std::string op_;
  std::unique_ptr<Expr> left_;
  std::unique_ptr<Expr> right_;

  DISALLOW_COPY_AND_ASSIGN(BinOp);
}; // class BinOp

// Used for e.g. variable literals and types.
// Syntex_syntax uses PathSegment, but we'll skip this for now. If there comes
// a time where I'll need them, I guess I'll add them in.
//
// For now, I'll treat this as if it was a single PathSegment_
class Path : public Expr {
 public:
  Path(const std::string& ident);

  void Write(CodeWriter* to) const override;
 private:
  std::string name_;

  DISALLOW_COPY_AND_ASSIGN(Path);
}; // class Path

// expr?
class Try : public Expr {
 public:
  Try(Expr *expr);
  Try(std::unique_ptr<Expr> expr);

  void Write(CodeWriter* to) const override;
 private:
  std::unique_ptr<Expr> expr_;

  DISALLOW_COPY_AND_ASSIGN(Try);
}; // class Try

// Function call, in the form of Some::Module::Path::test(args, args, args);
class Call : public Expr {
 public:
  // Handling the common case where fn is just a hard-coded string
  Call(const std::string &fn, std::vector<std::unique_ptr<Expr>> args);
  Call(Expr* fn, std::vector<std::unique_ptr<Expr>> args);

  void Write(CodeWriter* to) const override;
 private:
  std::unique_ptr<Expr> fn_;
  std::vector<std::unique_ptr<Expr>> args_;

  DISALLOW_COPY_AND_ASSIGN(Call);
}; // class Call

// Struct field access
class Field : public Expr {
 public:
  Field(Expr *on, const std::string &name);

  void Write(CodeWriter* to) const override;
 private:
  std::unique_ptr<Expr> on_;
  std::string name_;

  DISALLOW_COPY_AND_ASSIGN(Field);
}; // class Field

// A method call, in the form of arg0.fnName(arg1, arg2, arg3)
// TODO: I really think I should move arg0 out of the vector, since it is
// impossible to statically garantee the vec has at least 1 elt.
class MethodCall : public Expr {
 public:
  MethodCall(const std::string& name, std::vector<std::unique_ptr<Expr>> args);
  MethodCall(const std::string& name, Expr* self);

  void Write(CodeWriter* to) const override;
 private:
  std::string name_;
  std::vector<std::unique_ptr<Expr>> args_;
  DISALLOW_COPY_AND_ASSIGN(MethodCall);
}; // class MethodCall

// A block, in the form of unsafe { stmts; ret }
class Block : public Expr {
 public:
  Block(bool unsafe);
  virtual ~Block() = default;

  void AddStmt(Stmt* stmt); // Takes ownership
  void SetRet(Expr* expr);

  void Write(CodeWriter* to) const override;
  bool RequiresSemiToBeStmt() const override { return false; };
 private:
  bool unsafe_;
  std::vector<std::unique_ptr<Stmt>> stmts_;
  std::unique_ptr<Expr> ret_;

  DISALLOW_COPY_AND_ASSIGN(Block);
}; // class Block

// A match case
// TODO: Should take a Pattern instead of a string.
// Pat => Expr
class Arm : public AstNode {
 public:
  Arm(const std::string& pat, std::unique_ptr<Expr> expr);

  void Write(CodeWriter* to) const override;
 private:
  std::string pat_;
  std::unique_ptr<Expr> expr_;
  DISALLOW_COPY_AND_ASSIGN(Arm);
}; // class Arm

// A match statement
// match val { Case, Case, Case }
class Match : public Expr {
 public:
  Match(std::unique_ptr<Expr> val);

  void AddCase(Arm* case_);

  void Write(CodeWriter* to) const override;
  bool RequiresSemiToBeStmt() const override { return false; };
 private:
  std::unique_ptr<Expr> val_;
  std::vector<std::unique_ptr<Arm>> cases_;
  DISALLOW_COPY_AND_ASSIGN(Match);
}; // class Match

// An if statement
// if condition { block } else EXPR
class If : public Expr {
 public:
  If(Expr* condition);

  Block* GetIfBlock();
  void SetElse(Expr* expr);

  void Write(CodeWriter* to) const override;
  bool RequiresSemiToBeStmt() const override { return false; };
 private:
  std::unique_ptr<Expr> condition_;
  Block if_;
  // Potentially null
  std::unique_ptr<Expr> else_;

  DISALLOW_COPY_AND_ASSIGN(If);
}; // class If

// A field in a struct constructor
// fieldName: expr,
struct FieldCtr : public AstNode {
 public:
  FieldCtr(const std::string& field_name, Expr* expr);

  void Write(CodeWriter* to) const override;
 private:
  std::string field_name_;
  std::unique_ptr<Expr> expr_;
  DISALLOW_COPY_AND_ASSIGN(FieldCtr);
}; // class FieldCtr

// A struct constructor. Doesn't support ..pattern
// StructName { fields }
class StructCtr : public Expr {
 public:
  StructCtr(const std::string& struct_name, std::vector<std::unique_ptr<FieldCtr>> fields_);

  void Write(CodeWriter* to) const override;
 private:
  std::string struct_name_;
  std::vector<std::unique_ptr<FieldCtr>> fields_;
  DISALLOW_COPY_AND_ASSIGN(StructCtr);
}; // class StructCtr

// Any top-level declaration
// TODO: Visibility can be pub(crate), or restricted
class Item : public AstNode {
 public:
  Item(std::vector<std::string> attrs, bool pub);
  virtual ~Item() = default;

  void AddAttr(const std::string& attr);
  void Write(CodeWriter* to) const override;

 private:
  std::vector<std::string> attrs_;
  bool public_;
  DISALLOW_COPY_AND_ASSIGN(Item);
};  // class Item

// A struct field, in the form of fieldname: ty
class StructField : public AstNode {
 public:
  StructField(const std::string& name, const std::string& ty);
  virtual ~StructField() = default;

  void Write(CodeWriter* to) const override;

 private:
  std::string name_;
  std::string ty_;

  DISALLOW_COPY_AND_ASSIGN(StructField);
}; // class StructField

// A struct
// struct name { structfields }
class Struct : public Item {
 public:
  Struct(bool pub, const std::string& name);

  void Write(CodeWriter* to) const override;

  void AddField(StructField* member);

 private:
  std::string name_;
  std::vector<std::unique_ptr<StructField>> members_;

  DISALLOW_COPY_AND_ASSIGN(Struct);
};  // class Struct

// An enum variant. Doesn't take into account enum data, I don't need it.
// VariantName = Discr,
class EnumVariant : public AstNode {
 public:
  EnumVariant(const std::string& name);
  EnumVariant(const std::string& name, Expr *discr);

  void Write(CodeWriter* to) const override;
 private:
  std::string name_;
  // Can be null if there are no discr
  std::unique_ptr<Expr> discr_;
}; // class EnumVariant

// enum NAME { variants }
class Enum : public Item {
 public:
  Enum(bool pub, const std::string& name);

  void Write(CodeWriter* to) const override;
  void AddVariant(EnumVariant* variant);

 private:
  std::string name_;
  std::vector<std::unique_ptr<EnumVariant>> variants_;
  DISALLOW_COPY_AND_ASSIGN(Enum);
}; // class Enum

// Any item that can be in an impl block. Implements GetName to (eventually)
// prevent duplicate names
class ImplItem : public AstNode {
 public:
  ImplItem(bool vis, const std::string& name);
  virtual ~ImplItem() = default;

  std::string GetName() const;
  void Write(CodeWriter* to) const override;
 private:
  bool vis_;
  std::string name_;

  DISALLOW_COPY_AND_ASSIGN(ImplItem);
}; // class ImplItem

// A method impl argument.
// name: type,
class Arg : public AstNode {
 public:
  Arg(const std::string& name);
  Arg(const std::string& name, const std::string& type);

  void Write(CodeWriter* to) const override;
 private:
  std::string name_;
  bool infered_self_;
  std::string type_;

  DISALLOW_COPY_AND_ASSIGN(Arg);
}; // class Arg

// A method declaration
// fn fn_name(args) -> ret block
class MethodImpl : public ImplItem {
 public:
  MethodImpl(bool vis, const std::string& name, std::vector<std::unique_ptr<Arg>> args, const std::string& ret);
  MethodImpl(const std::string& name, std::vector<std::unique_ptr<Arg>> args, const std::string& ret);

  Block* GetBlock();

  void Write(CodeWriter* to) const override;

 private:
  std::vector<std::unique_ptr<Arg>> args_;
  std::string ret_type_;
  Block block_;

  DISALLOW_COPY_AND_ASSIGN(MethodImpl);
}; // class MethodImpl

// A use statement
// use some::Path;
class Use : public Item {
 public:
  Use(const std::string& path);

  void Write(CodeWriter* to) const override;

 private:
  std::string path_;
  DISALLOW_COPY_AND_ASSIGN(Use);
}; // class Use

// An impl block
// impl struct { implitems }
// impl trait for struct { implitems }
class Impl : public Item {
 public:
  Impl(const std::string& on);
  Impl(const std::string& on, const std::string& trait);

  void AddItem(std::unique_ptr<ImplItem> item);
  void Write(CodeWriter* to) const override;

 private:
  std::string on_;
  std::unique_ptr<std::string> trait_; // nullable

  std::vector<std::unique_ptr<ImplItem>> items_;
  DISALLOW_COPY_AND_ASSIGN(Impl);
}; // class Impl

// A series of item making up a rust source file.
class Document : public AstNode {
 public:
  Document(std::vector<std::unique_ptr<Item>> items);

  void Write(CodeWriter* to) const override;

 private:
  std::vector<std::unique_ptr<Item>> items_;

  DISALLOW_COPY_AND_ASSIGN(Document);
};  // class Document

}  // namespace rust
}  // namespace aidl
}  // namespace android

#endif // AIDL_AST_RUST_H_
