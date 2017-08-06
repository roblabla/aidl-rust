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

#include "generate_rust.h"

#include <cctype>
#include <cstring>
#include <memory>
#include <random>
#include <set>
#include <string>

#include <android-base/stringprintf.h>

#include "aidl_language.h"
#include "ast_rust.h"
#include "code_writer.h"
#include "logging.h"
#include "os.h"

using android::base::StringPrintf;
using std::string;
using std::unique_ptr;
using std::vector;
using std::set;

namespace android {
namespace aidl {
namespace rust {
namespace {

string Capitalize(const std::string& s) {
  string result = s;
  // TODO: Check s len > 0
  result[0] = toupper(result[0]);
  return result;
}

string BuildRetType(const TypeNamespace& types,
                    const AidlMethod& method)
{
  const Type* return_type = method.GetType().GetLanguageType<Type>();
  return StringPrintf("::binder::Result<%s>", return_type->RustOwnedType().c_str());
}

vector<unique_ptr<Arg>> BuildArgList(const TypeNamespace& types,
                                     const AidlMethod& method)
{
  vector<unique_ptr<Arg>> args;

  args.emplace_back(new Arg{"&mut self"});

  for (const unique_ptr<AidlArgument>& a : method.GetArguments()) {
    string type;
    string name;
    const Type* _type = a->GetType().GetLanguageType<Type>();

    if (a->IsOut()) {
      // Pass by mutable ref if it's an out type
      type = "&mut " + _type->RustOwnedType();
    } else {
      // Pass by ref if it's not a primitive
      type = _type->RustBorrowedType();
    }

    if (a->GetDirection() & AidlArgument::IN_DIR) {
      name = "in_" + a->GetName();
    } else {
      name = "out_" + a->GetName();
    }
    unique_ptr<Arg> arg = unique_ptr<Arg>{new Arg{name, type}};
    args.push_back(std::move(arg));
  }

  return args;
}

unique_ptr<ImplItem> DefineClientTransaction(const TypeNamespace& types,
                                             const AidlInterface& interface,
                                             const AidlMethod& method) {
  unique_ptr<MethodImpl> ret{new MethodImpl{true, method.GetName(),
    BuildArgList(types, method), BuildRetType(types, method)}};

  Block* b = ret->GetBlock();

  // let data = OwnedParcel::new(self.handle.borrow().conn.clone());
  {
    vector<unique_ptr<Expr>> args;
    args.emplace_back(new MethodCall("clone", new Field(new MethodCall("borrow", new Field(new Path("self"), "handle")), "conn")));
    b->AddStmt(new Local(Local::BYVAL_MUT, "data", unique_ptr<Expr>(new Call("::binder::OwnedParcel::new", std::move(args)))));
  }

  // data.write_interface_token(Interface::get_interface_descriptor())?;
  {
    vector<unique_ptr<Expr>> args;
    args.emplace_back(new Path("data"));
    args.emplace_back(new Call(StringPrintf("%s::get_interface_descriptor", interface.GetName().c_str()), vector<unique_ptr<Expr>>()));
    b->AddStmt(new StmtExpr(new MethodCall("write_interface_token", std::move(args))));
  }

  for (const auto& a: method.GetArguments()) {
    const Type* type = a->GetType().GetLanguageType<Type>();
    const string& var_name = a->GetName();
    if (a->IsIn()) {
      // data.write_i32(in_param_name)?;
      unique_ptr<Expr> expr = type->WriteToParcel("data", unique_ptr<Expr>(new Path("in_" + var_name)));
      b->AddStmt(new StmtExpr(std::move(expr)));
    } else {
      // TODO: Out and inout
      return nullptr;
    }
  }

  // let mut reply = self.handle.borrow_mut().transact(SomeProtocol::FunctionName as u32, &mut data, 0)?;
  {
    vector<unique_ptr<Expr>> args;
    args.emplace_back(new MethodCall("borrow_mut", new Field(new Path("self"), "handle")));
    args.emplace_back(new Cast(new Path(StringPrintf("%sProtocol::%s", interface.GetName().c_str(), Capitalize(method.GetName()).c_str())), "u32"));
    args.emplace_back(new AddrOf(true, new Path("data")));
    if (interface.IsOneway() || method.IsOneway()) {
      args.emplace_back(new Path("::binder::TransactionFlags::ONEWAY"));
    } else {
      args.emplace_back(new Lit("0"));
    }
    b->AddStmt(new Local(Local::BYVAL_MUT, "reply", unique_ptr<Expr>(new Try(new MethodCall("transact", std::move(args))))));
  }

  if(!interface.IsOneway() && !method.IsOneway()) {
    // reply.read_exception
    b->AddStmt(new StmtExpr(new Try(new MethodCall("read_exception", new Path("reply")))));
  }

  // let ret = data.read_method()?;
  const Type* return_type = method.GetType().GetLanguageType<Type>();
  if (return_type != types.VoidType()) {
    unique_ptr<Expr> read = return_type->ReadFromParcel("reply");
    b->AddStmt(new Local(Local::BYVAL, "ret", unique_ptr<Expr>(new Try(std::move(read)))));

    vector<unique_ptr<Expr>> args;
    args.emplace_back(new Path("ret"));
    b->SetRet(new Call("Ok", std::move(args)));
  } else {
    vector<unique_ptr<Expr>> args;
    args.emplace_back(new Lit("()"));
    b->SetRet(new Call("Ok", std::move(args)));
  }

  return unique_ptr<ImplItem>(ret.release());
}

// TODO: Figure out the namespacing story
unique_ptr<Document> BuildClientSource(const TypeNamespace& types,
                                       const AidlInterface& interface) {
  // TODO: Replace with use_list ?

  // #[derive(Debug)] struct Interface { handle: std::rc::Rc<::std::cell::RefCell<::binder::Handle>> }
  unique_ptr<Struct> struc = unique_ptr<Struct>{new Struct{
    true, interface.GetName()
  }};

  struc->AddAttr("derive(Debug)");
  struc->AddField(new StructField("handle", "::std::rc::Rc<::std::cell::RefCell<::binder::Handle>>"));

  // impl Interface {
  unique_ptr<Impl> struct_impl = unique_ptr<Impl>{new Impl{
    interface.GetName()
  }};

  for (const auto& method : interface.GetMethods()) {
    // Implement Interface method
    unique_ptr<ImplItem> i = DefineClientTransaction(types, interface, *method);
    if (!i) { return nullptr; }
    struct_impl->AddItem(std::move(i));
  }
  // }

  // impl IInterface for Interface {
  unique_ptr<Impl> iinterface_impl = unique_ptr<Impl>{new Impl{
    interface.GetName(),
    "::binder::IInterface"
  }};

  {
    // fn get_interface_descriptor() -> &'static str {
    unique_ptr<MethodImpl> method {new MethodImpl{"get_interface_descriptor",
      vector<unique_ptr<Arg>>(), "&'static str"}};
    Block* b = method->GetBlock();
    // "some.package.Interface"
    b->SetRet(new Lit(StringPrintf("\"%s.%s\"", interface.GetPackage().c_str(), interface.GetName().c_str())));
    iinterface_impl->AddItem(std::move(method));
    // }
  }
  {
    // fn from_handle(handle: Rc<RefCell<::Handle>>) -> Interface {
    vector<unique_ptr<Arg>> args;
    args.emplace_back(new Arg("handle", "::std::rc::Rc<::std::cell::RefCell<::binder::Handle>>"));
    unique_ptr<MethodImpl> method {new MethodImpl{"from_handle",
      std::move(args), interface.GetName()}};
    Block* b = method->GetBlock();
    // Interface { handle: handle }
    vector<unique_ptr<FieldCtr>> fields;
    fields.emplace_back(new FieldCtr("handle", new Path("handle")));
    b->SetRet(new StructCtr(interface.GetName(), std::move(fields)));
    iinterface_impl->AddItem(std::move(method));
    // }
  }

  vector<unique_ptr<Item>> items;
  items.push_back(std::move(struc));
  items.push_back(std::move(struct_impl));
  items.push_back(std::move(iinterface_impl));

  return unique_ptr<Document>{new Document {
    std::move(items)
  }};
}

unique_ptr<Document> BuildProtocolSource(const TypeNamespace& types,
                                         const AidlInterface& interface) {
  unique_ptr<Enum> proto = unique_ptr<Enum>{new Enum{
    true, StringPrintf("%sProtocol", interface.GetName().c_str())
  }};

  proto->AddAttr("derive(Debug)");
  proto->AddAttr("repr(i32)");
  for (const auto& method : interface.GetMethods()) {
    proto->AddVariant(new EnumVariant(Capitalize(method->GetName()), new BinOp("+", new Path("::binder::FIRST_CALL_TRANSACTION"), new Lit(StringPrintf("%d", method->GetId())))));
  }

  vector<unique_ptr<Item>> items;
  items.push_back(std::move(proto));
  return unique_ptr<Document>{new Document{
    std::move(items)
  }};
}

}

bool GenerateRust(const RustOptions& options,
                  const TypeNamespace& types,
                  const AidlInterface& interface,
                  const IoDelegate& io_delegate) {
  unique_ptr<Document> imports;
  {
    vector<unique_ptr<Item>> items;
    items.emplace_back(new Use("binder::Parcel"));
    items.emplace_back(new Use("binder::Parcelable"));
    items.emplace_back(new Use("binder::IInterface"));
    imports = unique_ptr<Document>(new Document{
        std::move(items)
    });
  }

  auto protocol_src = BuildProtocolSource(types, interface);
  auto client_src = BuildClientSource(types, interface);
  //auto server_src = BuildServerSource(types, interface);

  if (!protocol_src || !client_src/* || !server_src*/) {
    return false;
  }

  unique_ptr<CodeWriter> writer = io_delegate.GetCodeWriter(
      options.OutputRustFilePath());
  imports->Write(writer.get());
  protocol_src->Write(writer.get());
  client_src->Write(writer.get());
  //server_src->Write(writer.get());

  const bool success = writer->Close();
  if (!success) {
    io_delegate.RemovePath(options.OutputRustFilePath());
  }

  return success;
}

}  // namespace cpp
}  // namespace aidl
}  // namespace android
