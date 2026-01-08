// A Bison parser, made by GNU Bison 3.8.2.

// Skeleton implementation for Bison LALR(1) parsers in C++

// Copyright (C) 2002-2015, 2018-2021 Free Software Foundation, Inc.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// As a special exception, you may create a larger work that contains
// part or all of the Bison parser skeleton and distribute that work
// under terms of your choice, so long as that work isn't itself a
// parser generator using the skeleton or a modified version thereof
// as a parser skeleton.  Alternatively, if you modify or redistribute
// the parser skeleton itself, you may (at your option) remove this
// special exception, which will cause the skeleton and the resulting
// Bison output files to be licensed under the GNU General Public
// License without this special exception.

// This special exception was added by the Free Software Foundation in
// version 2.2 of Bison.

// DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
// especially those whose name start with YY_ or yy_.  They are
// private implementation details that can be changed or removed.





#include "parser.tab.h"


// Unqualified %code blocks.
#line 22 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"

    #include "lexer/lexer.hpp"
    #include "diagnostics/DiagnosticEngine.hpp"
    fin::parser::symbol_type yylex();
    
    namespace fin {
        std::unique_ptr<fin::Program> root;
    }
    std::string flatten_macro_name(fin::Expression* expr) {
        if (auto* id = dynamic_cast<fin::Identifier*>(expr)) {
            return id->name;
        }
        if (auto* mem = dynamic_cast<fin::MemberAccess*>(expr)) {
            std::string left = flatten_macro_name(mem->object.get());
            if (!left.empty()) return left + "." + mem->member;
        }
        return "";
    }

#line 66 "parser.tab.c"


#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> // FIXME: INFRINGES ON USER NAME SPACE.
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif


// Whether we are compiled with exception support.
#ifndef YY_EXCEPTIONS
# if defined __GNUC__ && !defined __EXCEPTIONS
#  define YY_EXCEPTIONS 0
# else
#  define YY_EXCEPTIONS 1
# endif
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K].location)
/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

# ifndef YYLLOC_DEFAULT
#  define YYLLOC_DEFAULT(Current, Rhs, N)                               \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).begin  = YYRHSLOC (Rhs, 1).begin;                   \
          (Current).end    = YYRHSLOC (Rhs, N).end;                     \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).begin = (Current).end = YYRHSLOC (Rhs, 0).end;      \
        }                                                               \
    while (false)
# endif


// Enable debugging if requested.
#if YYDEBUG

// A pseudo ostream that takes yydebug_ into account.
# define YYCDEBUG if (yydebug_) (*yycdebug_)

# define YY_SYMBOL_PRINT(Title, Symbol)         \
  do {                                          \
    if (yydebug_)                               \
    {                                           \
      *yycdebug_ << Title << ' ';               \
      yy_print_ (*yycdebug_, Symbol);           \
      *yycdebug_ << '\n';                       \
    }                                           \
  } while (false)

# define YY_REDUCE_PRINT(Rule)          \
  do {                                  \
    if (yydebug_)                       \
      yy_reduce_print_ (Rule);          \
  } while (false)

# define YY_STACK_PRINT()               \
  do {                                  \
    if (yydebug_)                       \
      yy_stack_print_ ();                \
  } while (false)

#else // !YYDEBUG

# define YYCDEBUG if (false) std::cerr
# define YY_SYMBOL_PRINT(Title, Symbol)  YY_USE (Symbol)
# define YY_REDUCE_PRINT(Rule)           static_cast<void> (0)
# define YY_STACK_PRINT()                static_cast<void> (0)

#endif // !YYDEBUG

#define yyerrok         (yyerrstatus_ = 0)
#define yyclearin       (yyla.clear ())

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYRECOVERING()  (!!yyerrstatus_)

#line 6 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
namespace fin {
#line 159 "parser.tab.c"

  /// Build a parser object.
  parser::parser (fin::DiagnosticEngine& diag_yyarg)
#if YYDEBUG
    : yydebug_ (false),
      yycdebug_ (&std::cerr),
#else
    :
#endif
      diag (diag_yyarg)
  {}

  parser::~parser ()
  {}

  parser::syntax_error::~syntax_error () YY_NOEXCEPT YY_NOTHROW
  {}

  /*---------.
  | symbol.  |
  `---------*/



  // by_state.
  parser::by_state::by_state () YY_NOEXCEPT
    : state (empty_state)
  {}

  parser::by_state::by_state (const by_state& that) YY_NOEXCEPT
    : state (that.state)
  {}

  void
  parser::by_state::clear () YY_NOEXCEPT
  {
    state = empty_state;
  }

  void
  parser::by_state::move (by_state& that)
  {
    state = that.state;
    that.clear ();
  }

  parser::by_state::by_state (state_type s) YY_NOEXCEPT
    : state (s)
  {}

  parser::symbol_kind_type
  parser::by_state::kind () const YY_NOEXCEPT
  {
    if (state == empty_state)
      return symbol_kind::S_YYEMPTY;
    else
      return YY_CAST (symbol_kind_type, yystos_[+state]);
  }

  parser::stack_symbol_type::stack_symbol_type ()
  {}

  parser::stack_symbol_type::stack_symbol_type (YY_RVREF (stack_symbol_type) that)
    : super_type (YY_MOVE (that.state), YY_MOVE (that.location))
  {
    switch (that.kind ())
    {
      case symbol_kind::S_visibility_opt: // visibility_opt
        value.YY_MOVE_OR_COPY< bool > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_operator_symbol: // operator_symbol
        value.YY_MOVE_OR_COPY< fin::ASTTokenKind > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_param: // macro_param
        value.YY_MOVE_OR_COPY< fin::MacroParam > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_rule: // macro_rule
        value.YY_MOVE_OR_COPY< fin::MacroRule > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_enum_value: // enum_value
        value.YY_MOVE_OR_COPY< std::pair<std::string, std::unique_ptr<fin::Expression>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_extern_params: // extern_params
        value.YY_MOVE_OR_COPY< std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_IDENTIFIER: // IDENTIFIER
      case symbol_kind::S_INTEGER: // INTEGER
      case symbol_kind::S_FLOAT: // FLOAT
      case symbol_kind::S_STRING_LITERAL: // STRING_LITERAL
      case symbol_kind::S_CHAR_LITERAL: // CHAR_LITERAL
      case symbol_kind::S_TYPE_ID: // TYPE_ID
      case symbol_kind::S_attr_id: // attr_id
      case symbol_kind::S_dotted_path: // dotted_path
      case symbol_kind::S_primitive_type: // primitive_type
        value.YY_MOVE_OR_COPY< std::string > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_struct_item_rest: // struct_item_rest
      case symbol_kind::S_interface_item_rest: // interface_item_rest
      case symbol_kind::S_implements_item_rest: // implements_item_rest
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::ASTNode> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_attribute: // attribute
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::Attribute> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_block: // block
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::Block> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_super_expression: // super_expression
      case symbol_kind::S_lambda_expression: // lambda_expression
      case symbol_kind::S_expression: // expression
      case symbol_kind::S_no_struct_expression: // no_struct_expression
      case symbol_kind::S_static_method_call: // static_method_call
      case symbol_kind::S_primary_no_struct: // primary_no_struct
      case symbol_kind::S_literal: // literal
      case symbol_kind::S_prototype_literal: // prototype_literal
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::Expression> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_generic_param: // generic_param
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::GenericParam> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_implements_body_content: // implements_body_content
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::ImplementsBlock> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_interface_body_content: // interface_body_content
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::InterfaceDeclaration> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_param: // param
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::Parameter> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_program: // program
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::Program> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_statement: // statement
      case symbol_kind::S_annotated_declaration: // annotated_declaration
      case symbol_kind::S_declaration_with_vis: // declaration_with_vis
      case symbol_kind::S_bare_declaration: // bare_declaration
      case symbol_kind::S_declaration_body: // declaration_body
      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_define_declaration: // define_declaration
      case symbol_kind::S_macro_declaration: // macro_declaration
      case symbol_kind::S_type_definition: // type_definition
      case symbol_kind::S_implements_block: // implements_block
      case symbol_kind::S_special_declaration: // special_declaration
      case symbol_kind::S_variable_declaration: // variable_declaration
      case symbol_kind::S_if_statement: // if_statement
      case symbol_kind::S_while_loop: // while_loop
      case symbol_kind::S_for_loop: // for_loop
      case symbol_kind::S_foreach_loop: // foreach_loop
      case symbol_kind::S_control_statement: // control_statement
      case symbol_kind::S_delete_statement: // delete_statement
      case symbol_kind::S_try_catch_statement: // try_catch_statement
      case symbol_kind::S_blame_statement: // blame_statement
      case symbol_kind::S_return_statement: // return_statement
      case symbol_kind::S_expression_statement: // expression_statement
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::Statement> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_struct_body_content: // struct_body_content
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::StructDeclaration> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_implements_opt: // implements_opt
      case symbol_kind::S_type: // type
      case symbol_kind::S_type_no_annot: // type_no_annot
      case symbol_kind::S_base_type: // base_type
      case symbol_kind::S_fn_type: // fn_type
      case symbol_kind::S_pointer_type: // pointer_type
      case symbol_kind::S_array_type: // array_type
        value.YY_MOVE_OR_COPY< std::unique_ptr<fin::TypeNode> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_param_list: // macro_param_list
        value.YY_MOVE_OR_COPY< std::vector<fin::MacroParam> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_rules: // macro_rules
        value.YY_MOVE_OR_COPY< std::vector<fin::MacroRule> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_field_assignments: // field_assignments
      case symbol_kind::S_enum_values: // enum_values
        value.YY_MOVE_OR_COPY< std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_prototype_elements: // prototype_elements
        value.YY_MOVE_OR_COPY< std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_import_list: // import_list
        value.YY_MOVE_OR_COPY< std::vector<std::string> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_attributes_opt: // attributes_opt
      case symbol_kind::S_attribute_list: // attribute_list
        value.YY_MOVE_OR_COPY< std::vector<std::unique_ptr<fin::Attribute>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_arguments: // arguments
      case symbol_kind::S_expression_list: // expression_list
      case symbol_kind::S_macro_arg_item: // macro_arg_item
      case symbol_kind::S_macro_arg_list_body: // macro_arg_list_body
      case symbol_kind::S_macro_arguments: // macro_arguments
        value.YY_MOVE_OR_COPY< std::vector<std::unique_ptr<fin::Expression>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_generic_params_opt: // generic_params_opt
      case symbol_kind::S_generic_param_list: // generic_param_list
      case symbol_kind::S_operator_generics_opt: // operator_generics_opt
        value.YY_MOVE_OR_COPY< std::vector<std::unique_ptr<fin::GenericParam>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_operator_params_opt: // operator_params_opt
      case symbol_kind::S_params: // params
      case symbol_kind::S_param_list: // param_list
        value.YY_MOVE_OR_COPY< std::vector<std::unique_ptr<fin::Parameter>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_statements: // statements
      case symbol_kind::S_block_stmts: // block_stmts
        value.YY_MOVE_OR_COPY< std::vector<std::unique_ptr<fin::Statement>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_inheritance_opt: // inheritance_opt
      case symbol_kind::S_type_list: // type_list
        value.YY_MOVE_OR_COPY< std::vector<std::unique_ptr<fin::TypeNode>> > (YY_MOVE (that.value));
        break;

      default:
        break;
    }

#if 201103L <= YY_CPLUSPLUS
    // that is emptied.
    that.state = empty_state;
#endif
  }

  parser::stack_symbol_type::stack_symbol_type (state_type s, YY_MOVE_REF (symbol_type) that)
    : super_type (s, YY_MOVE (that.location))
  {
    switch (that.kind ())
    {
      case symbol_kind::S_visibility_opt: // visibility_opt
        value.move< bool > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_operator_symbol: // operator_symbol
        value.move< fin::ASTTokenKind > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_param: // macro_param
        value.move< fin::MacroParam > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_rule: // macro_rule
        value.move< fin::MacroRule > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_enum_value: // enum_value
        value.move< std::pair<std::string, std::unique_ptr<fin::Expression>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_extern_params: // extern_params
        value.move< std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_IDENTIFIER: // IDENTIFIER
      case symbol_kind::S_INTEGER: // INTEGER
      case symbol_kind::S_FLOAT: // FLOAT
      case symbol_kind::S_STRING_LITERAL: // STRING_LITERAL
      case symbol_kind::S_CHAR_LITERAL: // CHAR_LITERAL
      case symbol_kind::S_TYPE_ID: // TYPE_ID
      case symbol_kind::S_attr_id: // attr_id
      case symbol_kind::S_dotted_path: // dotted_path
      case symbol_kind::S_primitive_type: // primitive_type
        value.move< std::string > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_struct_item_rest: // struct_item_rest
      case symbol_kind::S_interface_item_rest: // interface_item_rest
      case symbol_kind::S_implements_item_rest: // implements_item_rest
        value.move< std::unique_ptr<fin::ASTNode> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_attribute: // attribute
        value.move< std::unique_ptr<fin::Attribute> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_block: // block
        value.move< std::unique_ptr<fin::Block> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_super_expression: // super_expression
      case symbol_kind::S_lambda_expression: // lambda_expression
      case symbol_kind::S_expression: // expression
      case symbol_kind::S_no_struct_expression: // no_struct_expression
      case symbol_kind::S_static_method_call: // static_method_call
      case symbol_kind::S_primary_no_struct: // primary_no_struct
      case symbol_kind::S_literal: // literal
      case symbol_kind::S_prototype_literal: // prototype_literal
        value.move< std::unique_ptr<fin::Expression> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_generic_param: // generic_param
        value.move< std::unique_ptr<fin::GenericParam> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_implements_body_content: // implements_body_content
        value.move< std::unique_ptr<fin::ImplementsBlock> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_interface_body_content: // interface_body_content
        value.move< std::unique_ptr<fin::InterfaceDeclaration> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_param: // param
        value.move< std::unique_ptr<fin::Parameter> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_program: // program
        value.move< std::unique_ptr<fin::Program> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_statement: // statement
      case symbol_kind::S_annotated_declaration: // annotated_declaration
      case symbol_kind::S_declaration_with_vis: // declaration_with_vis
      case symbol_kind::S_bare_declaration: // bare_declaration
      case symbol_kind::S_declaration_body: // declaration_body
      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_define_declaration: // define_declaration
      case symbol_kind::S_macro_declaration: // macro_declaration
      case symbol_kind::S_type_definition: // type_definition
      case symbol_kind::S_implements_block: // implements_block
      case symbol_kind::S_special_declaration: // special_declaration
      case symbol_kind::S_variable_declaration: // variable_declaration
      case symbol_kind::S_if_statement: // if_statement
      case symbol_kind::S_while_loop: // while_loop
      case symbol_kind::S_for_loop: // for_loop
      case symbol_kind::S_foreach_loop: // foreach_loop
      case symbol_kind::S_control_statement: // control_statement
      case symbol_kind::S_delete_statement: // delete_statement
      case symbol_kind::S_try_catch_statement: // try_catch_statement
      case symbol_kind::S_blame_statement: // blame_statement
      case symbol_kind::S_return_statement: // return_statement
      case symbol_kind::S_expression_statement: // expression_statement
        value.move< std::unique_ptr<fin::Statement> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_struct_body_content: // struct_body_content
        value.move< std::unique_ptr<fin::StructDeclaration> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_implements_opt: // implements_opt
      case symbol_kind::S_type: // type
      case symbol_kind::S_type_no_annot: // type_no_annot
      case symbol_kind::S_base_type: // base_type
      case symbol_kind::S_fn_type: // fn_type
      case symbol_kind::S_pointer_type: // pointer_type
      case symbol_kind::S_array_type: // array_type
        value.move< std::unique_ptr<fin::TypeNode> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_param_list: // macro_param_list
        value.move< std::vector<fin::MacroParam> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_rules: // macro_rules
        value.move< std::vector<fin::MacroRule> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_field_assignments: // field_assignments
      case symbol_kind::S_enum_values: // enum_values
        value.move< std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_prototype_elements: // prototype_elements
        value.move< std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_import_list: // import_list
        value.move< std::vector<std::string> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_attributes_opt: // attributes_opt
      case symbol_kind::S_attribute_list: // attribute_list
        value.move< std::vector<std::unique_ptr<fin::Attribute>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_arguments: // arguments
      case symbol_kind::S_expression_list: // expression_list
      case symbol_kind::S_macro_arg_item: // macro_arg_item
      case symbol_kind::S_macro_arg_list_body: // macro_arg_list_body
      case symbol_kind::S_macro_arguments: // macro_arguments
        value.move< std::vector<std::unique_ptr<fin::Expression>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_generic_params_opt: // generic_params_opt
      case symbol_kind::S_generic_param_list: // generic_param_list
      case symbol_kind::S_operator_generics_opt: // operator_generics_opt
        value.move< std::vector<std::unique_ptr<fin::GenericParam>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_operator_params_opt: // operator_params_opt
      case symbol_kind::S_params: // params
      case symbol_kind::S_param_list: // param_list
        value.move< std::vector<std::unique_ptr<fin::Parameter>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_statements: // statements
      case symbol_kind::S_block_stmts: // block_stmts
        value.move< std::vector<std::unique_ptr<fin::Statement>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_inheritance_opt: // inheritance_opt
      case symbol_kind::S_type_list: // type_list
        value.move< std::vector<std::unique_ptr<fin::TypeNode>> > (YY_MOVE (that.value));
        break;

      default:
        break;
    }

    // that is emptied.
    that.kind_ = symbol_kind::S_YYEMPTY;
  }

#if YY_CPLUSPLUS < 201103L
  parser::stack_symbol_type&
  parser::stack_symbol_type::operator= (const stack_symbol_type& that)
  {
    state = that.state;
    switch (that.kind ())
    {
      case symbol_kind::S_visibility_opt: // visibility_opt
        value.copy< bool > (that.value);
        break;

      case symbol_kind::S_operator_symbol: // operator_symbol
        value.copy< fin::ASTTokenKind > (that.value);
        break;

      case symbol_kind::S_macro_param: // macro_param
        value.copy< fin::MacroParam > (that.value);
        break;

      case symbol_kind::S_macro_rule: // macro_rule
        value.copy< fin::MacroRule > (that.value);
        break;

      case symbol_kind::S_enum_value: // enum_value
        value.copy< std::pair<std::string, std::unique_ptr<fin::Expression>> > (that.value);
        break;

      case symbol_kind::S_extern_params: // extern_params
        value.copy< std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > (that.value);
        break;

      case symbol_kind::S_IDENTIFIER: // IDENTIFIER
      case symbol_kind::S_INTEGER: // INTEGER
      case symbol_kind::S_FLOAT: // FLOAT
      case symbol_kind::S_STRING_LITERAL: // STRING_LITERAL
      case symbol_kind::S_CHAR_LITERAL: // CHAR_LITERAL
      case symbol_kind::S_TYPE_ID: // TYPE_ID
      case symbol_kind::S_attr_id: // attr_id
      case symbol_kind::S_dotted_path: // dotted_path
      case symbol_kind::S_primitive_type: // primitive_type
        value.copy< std::string > (that.value);
        break;

      case symbol_kind::S_struct_item_rest: // struct_item_rest
      case symbol_kind::S_interface_item_rest: // interface_item_rest
      case symbol_kind::S_implements_item_rest: // implements_item_rest
        value.copy< std::unique_ptr<fin::ASTNode> > (that.value);
        break;

      case symbol_kind::S_attribute: // attribute
        value.copy< std::unique_ptr<fin::Attribute> > (that.value);
        break;

      case symbol_kind::S_block: // block
        value.copy< std::unique_ptr<fin::Block> > (that.value);
        break;

      case symbol_kind::S_super_expression: // super_expression
      case symbol_kind::S_lambda_expression: // lambda_expression
      case symbol_kind::S_expression: // expression
      case symbol_kind::S_no_struct_expression: // no_struct_expression
      case symbol_kind::S_static_method_call: // static_method_call
      case symbol_kind::S_primary_no_struct: // primary_no_struct
      case symbol_kind::S_literal: // literal
      case symbol_kind::S_prototype_literal: // prototype_literal
        value.copy< std::unique_ptr<fin::Expression> > (that.value);
        break;

      case symbol_kind::S_generic_param: // generic_param
        value.copy< std::unique_ptr<fin::GenericParam> > (that.value);
        break;

      case symbol_kind::S_implements_body_content: // implements_body_content
        value.copy< std::unique_ptr<fin::ImplementsBlock> > (that.value);
        break;

      case symbol_kind::S_interface_body_content: // interface_body_content
        value.copy< std::unique_ptr<fin::InterfaceDeclaration> > (that.value);
        break;

      case symbol_kind::S_param: // param
        value.copy< std::unique_ptr<fin::Parameter> > (that.value);
        break;

      case symbol_kind::S_program: // program
        value.copy< std::unique_ptr<fin::Program> > (that.value);
        break;

      case symbol_kind::S_statement: // statement
      case symbol_kind::S_annotated_declaration: // annotated_declaration
      case symbol_kind::S_declaration_with_vis: // declaration_with_vis
      case symbol_kind::S_bare_declaration: // bare_declaration
      case symbol_kind::S_declaration_body: // declaration_body
      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_define_declaration: // define_declaration
      case symbol_kind::S_macro_declaration: // macro_declaration
      case symbol_kind::S_type_definition: // type_definition
      case symbol_kind::S_implements_block: // implements_block
      case symbol_kind::S_special_declaration: // special_declaration
      case symbol_kind::S_variable_declaration: // variable_declaration
      case symbol_kind::S_if_statement: // if_statement
      case symbol_kind::S_while_loop: // while_loop
      case symbol_kind::S_for_loop: // for_loop
      case symbol_kind::S_foreach_loop: // foreach_loop
      case symbol_kind::S_control_statement: // control_statement
      case symbol_kind::S_delete_statement: // delete_statement
      case symbol_kind::S_try_catch_statement: // try_catch_statement
      case symbol_kind::S_blame_statement: // blame_statement
      case symbol_kind::S_return_statement: // return_statement
      case symbol_kind::S_expression_statement: // expression_statement
        value.copy< std::unique_ptr<fin::Statement> > (that.value);
        break;

      case symbol_kind::S_struct_body_content: // struct_body_content
        value.copy< std::unique_ptr<fin::StructDeclaration> > (that.value);
        break;

      case symbol_kind::S_implements_opt: // implements_opt
      case symbol_kind::S_type: // type
      case symbol_kind::S_type_no_annot: // type_no_annot
      case symbol_kind::S_base_type: // base_type
      case symbol_kind::S_fn_type: // fn_type
      case symbol_kind::S_pointer_type: // pointer_type
      case symbol_kind::S_array_type: // array_type
        value.copy< std::unique_ptr<fin::TypeNode> > (that.value);
        break;

      case symbol_kind::S_macro_param_list: // macro_param_list
        value.copy< std::vector<fin::MacroParam> > (that.value);
        break;

      case symbol_kind::S_macro_rules: // macro_rules
        value.copy< std::vector<fin::MacroRule> > (that.value);
        break;

      case symbol_kind::S_field_assignments: // field_assignments
      case symbol_kind::S_enum_values: // enum_values
        value.copy< std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > (that.value);
        break;

      case symbol_kind::S_prototype_elements: // prototype_elements
        value.copy< std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > (that.value);
        break;

      case symbol_kind::S_import_list: // import_list
        value.copy< std::vector<std::string> > (that.value);
        break;

      case symbol_kind::S_attributes_opt: // attributes_opt
      case symbol_kind::S_attribute_list: // attribute_list
        value.copy< std::vector<std::unique_ptr<fin::Attribute>> > (that.value);
        break;

      case symbol_kind::S_arguments: // arguments
      case symbol_kind::S_expression_list: // expression_list
      case symbol_kind::S_macro_arg_item: // macro_arg_item
      case symbol_kind::S_macro_arg_list_body: // macro_arg_list_body
      case symbol_kind::S_macro_arguments: // macro_arguments
        value.copy< std::vector<std::unique_ptr<fin::Expression>> > (that.value);
        break;

      case symbol_kind::S_generic_params_opt: // generic_params_opt
      case symbol_kind::S_generic_param_list: // generic_param_list
      case symbol_kind::S_operator_generics_opt: // operator_generics_opt
        value.copy< std::vector<std::unique_ptr<fin::GenericParam>> > (that.value);
        break;

      case symbol_kind::S_operator_params_opt: // operator_params_opt
      case symbol_kind::S_params: // params
      case symbol_kind::S_param_list: // param_list
        value.copy< std::vector<std::unique_ptr<fin::Parameter>> > (that.value);
        break;

      case symbol_kind::S_statements: // statements
      case symbol_kind::S_block_stmts: // block_stmts
        value.copy< std::vector<std::unique_ptr<fin::Statement>> > (that.value);
        break;

      case symbol_kind::S_inheritance_opt: // inheritance_opt
      case symbol_kind::S_type_list: // type_list
        value.copy< std::vector<std::unique_ptr<fin::TypeNode>> > (that.value);
        break;

      default:
        break;
    }

    location = that.location;
    return *this;
  }

  parser::stack_symbol_type&
  parser::stack_symbol_type::operator= (stack_symbol_type& that)
  {
    state = that.state;
    switch (that.kind ())
    {
      case symbol_kind::S_visibility_opt: // visibility_opt
        value.move< bool > (that.value);
        break;

      case symbol_kind::S_operator_symbol: // operator_symbol
        value.move< fin::ASTTokenKind > (that.value);
        break;

      case symbol_kind::S_macro_param: // macro_param
        value.move< fin::MacroParam > (that.value);
        break;

      case symbol_kind::S_macro_rule: // macro_rule
        value.move< fin::MacroRule > (that.value);
        break;

      case symbol_kind::S_enum_value: // enum_value
        value.move< std::pair<std::string, std::unique_ptr<fin::Expression>> > (that.value);
        break;

      case symbol_kind::S_extern_params: // extern_params
        value.move< std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > (that.value);
        break;

      case symbol_kind::S_IDENTIFIER: // IDENTIFIER
      case symbol_kind::S_INTEGER: // INTEGER
      case symbol_kind::S_FLOAT: // FLOAT
      case symbol_kind::S_STRING_LITERAL: // STRING_LITERAL
      case symbol_kind::S_CHAR_LITERAL: // CHAR_LITERAL
      case symbol_kind::S_TYPE_ID: // TYPE_ID
      case symbol_kind::S_attr_id: // attr_id
      case symbol_kind::S_dotted_path: // dotted_path
      case symbol_kind::S_primitive_type: // primitive_type
        value.move< std::string > (that.value);
        break;

      case symbol_kind::S_struct_item_rest: // struct_item_rest
      case symbol_kind::S_interface_item_rest: // interface_item_rest
      case symbol_kind::S_implements_item_rest: // implements_item_rest
        value.move< std::unique_ptr<fin::ASTNode> > (that.value);
        break;

      case symbol_kind::S_attribute: // attribute
        value.move< std::unique_ptr<fin::Attribute> > (that.value);
        break;

      case symbol_kind::S_block: // block
        value.move< std::unique_ptr<fin::Block> > (that.value);
        break;

      case symbol_kind::S_super_expression: // super_expression
      case symbol_kind::S_lambda_expression: // lambda_expression
      case symbol_kind::S_expression: // expression
      case symbol_kind::S_no_struct_expression: // no_struct_expression
      case symbol_kind::S_static_method_call: // static_method_call
      case symbol_kind::S_primary_no_struct: // primary_no_struct
      case symbol_kind::S_literal: // literal
      case symbol_kind::S_prototype_literal: // prototype_literal
        value.move< std::unique_ptr<fin::Expression> > (that.value);
        break;

      case symbol_kind::S_generic_param: // generic_param
        value.move< std::unique_ptr<fin::GenericParam> > (that.value);
        break;

      case symbol_kind::S_implements_body_content: // implements_body_content
        value.move< std::unique_ptr<fin::ImplementsBlock> > (that.value);
        break;

      case symbol_kind::S_interface_body_content: // interface_body_content
        value.move< std::unique_ptr<fin::InterfaceDeclaration> > (that.value);
        break;

      case symbol_kind::S_param: // param
        value.move< std::unique_ptr<fin::Parameter> > (that.value);
        break;

      case symbol_kind::S_program: // program
        value.move< std::unique_ptr<fin::Program> > (that.value);
        break;

      case symbol_kind::S_statement: // statement
      case symbol_kind::S_annotated_declaration: // annotated_declaration
      case symbol_kind::S_declaration_with_vis: // declaration_with_vis
      case symbol_kind::S_bare_declaration: // bare_declaration
      case symbol_kind::S_declaration_body: // declaration_body
      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_define_declaration: // define_declaration
      case symbol_kind::S_macro_declaration: // macro_declaration
      case symbol_kind::S_type_definition: // type_definition
      case symbol_kind::S_implements_block: // implements_block
      case symbol_kind::S_special_declaration: // special_declaration
      case symbol_kind::S_variable_declaration: // variable_declaration
      case symbol_kind::S_if_statement: // if_statement
      case symbol_kind::S_while_loop: // while_loop
      case symbol_kind::S_for_loop: // for_loop
      case symbol_kind::S_foreach_loop: // foreach_loop
      case symbol_kind::S_control_statement: // control_statement
      case symbol_kind::S_delete_statement: // delete_statement
      case symbol_kind::S_try_catch_statement: // try_catch_statement
      case symbol_kind::S_blame_statement: // blame_statement
      case symbol_kind::S_return_statement: // return_statement
      case symbol_kind::S_expression_statement: // expression_statement
        value.move< std::unique_ptr<fin::Statement> > (that.value);
        break;

      case symbol_kind::S_struct_body_content: // struct_body_content
        value.move< std::unique_ptr<fin::StructDeclaration> > (that.value);
        break;

      case symbol_kind::S_implements_opt: // implements_opt
      case symbol_kind::S_type: // type
      case symbol_kind::S_type_no_annot: // type_no_annot
      case symbol_kind::S_base_type: // base_type
      case symbol_kind::S_fn_type: // fn_type
      case symbol_kind::S_pointer_type: // pointer_type
      case symbol_kind::S_array_type: // array_type
        value.move< std::unique_ptr<fin::TypeNode> > (that.value);
        break;

      case symbol_kind::S_macro_param_list: // macro_param_list
        value.move< std::vector<fin::MacroParam> > (that.value);
        break;

      case symbol_kind::S_macro_rules: // macro_rules
        value.move< std::vector<fin::MacroRule> > (that.value);
        break;

      case symbol_kind::S_field_assignments: // field_assignments
      case symbol_kind::S_enum_values: // enum_values
        value.move< std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > (that.value);
        break;

      case symbol_kind::S_prototype_elements: // prototype_elements
        value.move< std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > (that.value);
        break;

      case symbol_kind::S_import_list: // import_list
        value.move< std::vector<std::string> > (that.value);
        break;

      case symbol_kind::S_attributes_opt: // attributes_opt
      case symbol_kind::S_attribute_list: // attribute_list
        value.move< std::vector<std::unique_ptr<fin::Attribute>> > (that.value);
        break;

      case symbol_kind::S_arguments: // arguments
      case symbol_kind::S_expression_list: // expression_list
      case symbol_kind::S_macro_arg_item: // macro_arg_item
      case symbol_kind::S_macro_arg_list_body: // macro_arg_list_body
      case symbol_kind::S_macro_arguments: // macro_arguments
        value.move< std::vector<std::unique_ptr<fin::Expression>> > (that.value);
        break;

      case symbol_kind::S_generic_params_opt: // generic_params_opt
      case symbol_kind::S_generic_param_list: // generic_param_list
      case symbol_kind::S_operator_generics_opt: // operator_generics_opt
        value.move< std::vector<std::unique_ptr<fin::GenericParam>> > (that.value);
        break;

      case symbol_kind::S_operator_params_opt: // operator_params_opt
      case symbol_kind::S_params: // params
      case symbol_kind::S_param_list: // param_list
        value.move< std::vector<std::unique_ptr<fin::Parameter>> > (that.value);
        break;

      case symbol_kind::S_statements: // statements
      case symbol_kind::S_block_stmts: // block_stmts
        value.move< std::vector<std::unique_ptr<fin::Statement>> > (that.value);
        break;

      case symbol_kind::S_inheritance_opt: // inheritance_opt
      case symbol_kind::S_type_list: // type_list
        value.move< std::vector<std::unique_ptr<fin::TypeNode>> > (that.value);
        break;

      default:
        break;
    }

    location = that.location;
    // that is emptied.
    that.state = empty_state;
    return *this;
  }
#endif

  template <typename Base>
  void
  parser::yy_destroy_ (const char* yymsg, basic_symbol<Base>& yysym) const
  {
    if (yymsg)
      YY_SYMBOL_PRINT (yymsg, yysym);
  }

#if YYDEBUG
  template <typename Base>
  void
  parser::yy_print_ (std::ostream& yyo, const basic_symbol<Base>& yysym) const
  {
    std::ostream& yyoutput = yyo;
    YY_USE (yyoutput);
    if (yysym.empty ())
      yyo << "empty symbol";
    else
      {
        symbol_kind_type yykind = yysym.kind ();
        yyo << (yykind < YYNTOKENS ? "token" : "nterm")
            << ' ' << yysym.name () << " ("
            << yysym.location << ": ";
        YY_USE (yykind);
        yyo << ')';
      }
  }
#endif

  void
  parser::yypush_ (const char* m, YY_MOVE_REF (stack_symbol_type) sym)
  {
    if (m)
      YY_SYMBOL_PRINT (m, sym);
    yystack_.push (YY_MOVE (sym));
  }

  void
  parser::yypush_ (const char* m, state_type s, YY_MOVE_REF (symbol_type) sym)
  {
#if 201103L <= YY_CPLUSPLUS
    yypush_ (m, stack_symbol_type (s, std::move (sym)));
#else
    stack_symbol_type ss (s, sym);
    yypush_ (m, ss);
#endif
  }

  void
  parser::yypop_ (int n) YY_NOEXCEPT
  {
    yystack_.pop (n);
  }

#if YYDEBUG
  std::ostream&
  parser::debug_stream () const
  {
    return *yycdebug_;
  }

  void
  parser::set_debug_stream (std::ostream& o)
  {
    yycdebug_ = &o;
  }


  parser::debug_level_type
  parser::debug_level () const
  {
    return yydebug_;
  }

  void
  parser::set_debug_level (debug_level_type l)
  {
    yydebug_ = l;
  }
#endif // YYDEBUG

  parser::state_type
  parser::yy_lr_goto_state_ (state_type yystate, int yysym)
  {
    int yyr = yypgoto_[yysym - YYNTOKENS] + yystate;
    if (0 <= yyr && yyr <= yylast_ && yycheck_[yyr] == yystate)
      return yytable_[yyr];
    else
      return yydefgoto_[yysym - YYNTOKENS];
  }

  bool
  parser::yy_pact_value_is_default_ (int yyvalue) YY_NOEXCEPT
  {
    return yyvalue == yypact_ninf_;
  }

  bool
  parser::yy_table_value_is_error_ (int yyvalue) YY_NOEXCEPT
  {
    return yyvalue == yytable_ninf_;
  }

  int
  parser::operator() ()
  {
    return parse ();
  }

  int
  parser::parse ()
  {
    int yyn;
    /// Length of the RHS of the rule being reduced.
    int yylen = 0;

    // Error handling.
    int yynerrs_ = 0;
    int yyerrstatus_ = 0;

    /// The lookahead symbol.
    symbol_type yyla;

    /// The locations where the error started and ended.
    stack_symbol_type yyerror_range[3];

    /// The return value of parse ().
    int yyresult;

#if YY_EXCEPTIONS
    try
#endif // YY_EXCEPTIONS
      {
    YYCDEBUG << "Starting parse\n";


    /* Initialize the stack.  The initial state will be set in
       yynewstate, since the latter expects the semantical and the
       location values to have been already stored, initialize these
       stacks with a primary value.  */
    yystack_.clear ();
    yypush_ (YY_NULLPTR, 0, YY_MOVE (yyla));

  /*-----------------------------------------------.
  | yynewstate -- push a new symbol on the stack.  |
  `-----------------------------------------------*/
  yynewstate:
    YYCDEBUG << "Entering state " << int (yystack_[0].state) << '\n';
    YY_STACK_PRINT ();

    // Accept?
    if (yystack_[0].state == yyfinal_)
      YYACCEPT;

    goto yybackup;


  /*-----------.
  | yybackup.  |
  `-----------*/
  yybackup:
    // Try to take a decision without lookahead.
    yyn = yypact_[+yystack_[0].state];
    if (yy_pact_value_is_default_ (yyn))
      goto yydefault;

    // Read a lookahead token.
    if (yyla.empty ())
      {
        YYCDEBUG << "Reading a token\n";
#if YY_EXCEPTIONS
        try
#endif // YY_EXCEPTIONS
          {
            symbol_type yylookahead (yylex ());
            yyla.move (yylookahead);
          }
#if YY_EXCEPTIONS
        catch (const syntax_error& yyexc)
          {
            YYCDEBUG << "Caught exception: " << yyexc.what() << '\n';
            error (yyexc);
            goto yyerrlab1;
          }
#endif // YY_EXCEPTIONS
      }
    YY_SYMBOL_PRINT ("Next token is", yyla);

    if (yyla.kind () == symbol_kind::S_YYerror)
    {
      // The scanner already issued an error message, process directly
      // to error recovery.  But do not keep the error token as
      // lookahead, it is too special and may lead us to an endless
      // loop in error recovery. */
      yyla.kind_ = symbol_kind::S_YYUNDEF;
      goto yyerrlab1;
    }

    /* If the proper action on seeing token YYLA.TYPE is to reduce or
       to detect an error, take that action.  */
    yyn += yyla.kind ();
    if (yyn < 0 || yylast_ < yyn || yycheck_[yyn] != yyla.kind ())
      {
        goto yydefault;
      }

    // Reduce or error.
    yyn = yytable_[yyn];
    if (yyn <= 0)
      {
        if (yy_table_value_is_error_ (yyn))
          goto yyerrlab;
        yyn = -yyn;
        goto yyreduce;
      }

    // Count tokens shifted since error; after three, turn off error status.
    if (yyerrstatus_)
      --yyerrstatus_;

    // Shift the lookahead token.
    yypush_ ("Shifting", state_type (yyn), YY_MOVE (yyla));
    goto yynewstate;


  /*-----------------------------------------------------------.
  | yydefault -- do the default action for the current state.  |
  `-----------------------------------------------------------*/
  yydefault:
    yyn = yydefact_[+yystack_[0].state];
    if (yyn == 0)
      goto yyerrlab;
    goto yyreduce;


  /*-----------------------------.
  | yyreduce -- do a reduction.  |
  `-----------------------------*/
  yyreduce:
    yylen = yyr2_[yyn];
    {
      stack_symbol_type yylhs;
      yylhs.state = yy_lr_goto_state_ (yystack_[yylen].state, yyr1_[yyn]);
      /* Variants are always initialized to an empty instance of the
         correct type. The default '$$ = $1' action is NOT applied
         when using variants.  */
      switch (yyr1_[yyn])
    {
      case symbol_kind::S_visibility_opt: // visibility_opt
        yylhs.value.emplace< bool > ();
        break;

      case symbol_kind::S_operator_symbol: // operator_symbol
        yylhs.value.emplace< fin::ASTTokenKind > ();
        break;

      case symbol_kind::S_macro_param: // macro_param
        yylhs.value.emplace< fin::MacroParam > ();
        break;

      case symbol_kind::S_macro_rule: // macro_rule
        yylhs.value.emplace< fin::MacroRule > ();
        break;

      case symbol_kind::S_enum_value: // enum_value
        yylhs.value.emplace< std::pair<std::string, std::unique_ptr<fin::Expression>> > ();
        break;

      case symbol_kind::S_extern_params: // extern_params
        yylhs.value.emplace< std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > ();
        break;

      case symbol_kind::S_IDENTIFIER: // IDENTIFIER
      case symbol_kind::S_INTEGER: // INTEGER
      case symbol_kind::S_FLOAT: // FLOAT
      case symbol_kind::S_STRING_LITERAL: // STRING_LITERAL
      case symbol_kind::S_CHAR_LITERAL: // CHAR_LITERAL
      case symbol_kind::S_TYPE_ID: // TYPE_ID
      case symbol_kind::S_attr_id: // attr_id
      case symbol_kind::S_dotted_path: // dotted_path
      case symbol_kind::S_primitive_type: // primitive_type
        yylhs.value.emplace< std::string > ();
        break;

      case symbol_kind::S_struct_item_rest: // struct_item_rest
      case symbol_kind::S_interface_item_rest: // interface_item_rest
      case symbol_kind::S_implements_item_rest: // implements_item_rest
        yylhs.value.emplace< std::unique_ptr<fin::ASTNode> > ();
        break;

      case symbol_kind::S_attribute: // attribute
        yylhs.value.emplace< std::unique_ptr<fin::Attribute> > ();
        break;

      case symbol_kind::S_block: // block
        yylhs.value.emplace< std::unique_ptr<fin::Block> > ();
        break;

      case symbol_kind::S_super_expression: // super_expression
      case symbol_kind::S_lambda_expression: // lambda_expression
      case symbol_kind::S_expression: // expression
      case symbol_kind::S_no_struct_expression: // no_struct_expression
      case symbol_kind::S_static_method_call: // static_method_call
      case symbol_kind::S_primary_no_struct: // primary_no_struct
      case symbol_kind::S_literal: // literal
      case symbol_kind::S_prototype_literal: // prototype_literal
        yylhs.value.emplace< std::unique_ptr<fin::Expression> > ();
        break;

      case symbol_kind::S_generic_param: // generic_param
        yylhs.value.emplace< std::unique_ptr<fin::GenericParam> > ();
        break;

      case symbol_kind::S_implements_body_content: // implements_body_content
        yylhs.value.emplace< std::unique_ptr<fin::ImplementsBlock> > ();
        break;

      case symbol_kind::S_interface_body_content: // interface_body_content
        yylhs.value.emplace< std::unique_ptr<fin::InterfaceDeclaration> > ();
        break;

      case symbol_kind::S_param: // param
        yylhs.value.emplace< std::unique_ptr<fin::Parameter> > ();
        break;

      case symbol_kind::S_program: // program
        yylhs.value.emplace< std::unique_ptr<fin::Program> > ();
        break;

      case symbol_kind::S_statement: // statement
      case symbol_kind::S_annotated_declaration: // annotated_declaration
      case symbol_kind::S_declaration_with_vis: // declaration_with_vis
      case symbol_kind::S_bare_declaration: // bare_declaration
      case symbol_kind::S_declaration_body: // declaration_body
      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_define_declaration: // define_declaration
      case symbol_kind::S_macro_declaration: // macro_declaration
      case symbol_kind::S_type_definition: // type_definition
      case symbol_kind::S_implements_block: // implements_block
      case symbol_kind::S_special_declaration: // special_declaration
      case symbol_kind::S_variable_declaration: // variable_declaration
      case symbol_kind::S_if_statement: // if_statement
      case symbol_kind::S_while_loop: // while_loop
      case symbol_kind::S_for_loop: // for_loop
      case symbol_kind::S_foreach_loop: // foreach_loop
      case symbol_kind::S_control_statement: // control_statement
      case symbol_kind::S_delete_statement: // delete_statement
      case symbol_kind::S_try_catch_statement: // try_catch_statement
      case symbol_kind::S_blame_statement: // blame_statement
      case symbol_kind::S_return_statement: // return_statement
      case symbol_kind::S_expression_statement: // expression_statement
        yylhs.value.emplace< std::unique_ptr<fin::Statement> > ();
        break;

      case symbol_kind::S_struct_body_content: // struct_body_content
        yylhs.value.emplace< std::unique_ptr<fin::StructDeclaration> > ();
        break;

      case symbol_kind::S_implements_opt: // implements_opt
      case symbol_kind::S_type: // type
      case symbol_kind::S_type_no_annot: // type_no_annot
      case symbol_kind::S_base_type: // base_type
      case symbol_kind::S_fn_type: // fn_type
      case symbol_kind::S_pointer_type: // pointer_type
      case symbol_kind::S_array_type: // array_type
        yylhs.value.emplace< std::unique_ptr<fin::TypeNode> > ();
        break;

      case symbol_kind::S_macro_param_list: // macro_param_list
        yylhs.value.emplace< std::vector<fin::MacroParam> > ();
        break;

      case symbol_kind::S_macro_rules: // macro_rules
        yylhs.value.emplace< std::vector<fin::MacroRule> > ();
        break;

      case symbol_kind::S_field_assignments: // field_assignments
      case symbol_kind::S_enum_values: // enum_values
        yylhs.value.emplace< std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ();
        break;

      case symbol_kind::S_prototype_elements: // prototype_elements
        yylhs.value.emplace< std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > ();
        break;

      case symbol_kind::S_import_list: // import_list
        yylhs.value.emplace< std::vector<std::string> > ();
        break;

      case symbol_kind::S_attributes_opt: // attributes_opt
      case symbol_kind::S_attribute_list: // attribute_list
        yylhs.value.emplace< std::vector<std::unique_ptr<fin::Attribute>> > ();
        break;

      case symbol_kind::S_arguments: // arguments
      case symbol_kind::S_expression_list: // expression_list
      case symbol_kind::S_macro_arg_item: // macro_arg_item
      case symbol_kind::S_macro_arg_list_body: // macro_arg_list_body
      case symbol_kind::S_macro_arguments: // macro_arguments
        yylhs.value.emplace< std::vector<std::unique_ptr<fin::Expression>> > ();
        break;

      case symbol_kind::S_generic_params_opt: // generic_params_opt
      case symbol_kind::S_generic_param_list: // generic_param_list
      case symbol_kind::S_operator_generics_opt: // operator_generics_opt
        yylhs.value.emplace< std::vector<std::unique_ptr<fin::GenericParam>> > ();
        break;

      case symbol_kind::S_operator_params_opt: // operator_params_opt
      case symbol_kind::S_params: // params
      case symbol_kind::S_param_list: // param_list
        yylhs.value.emplace< std::vector<std::unique_ptr<fin::Parameter>> > ();
        break;

      case symbol_kind::S_statements: // statements
      case symbol_kind::S_block_stmts: // block_stmts
        yylhs.value.emplace< std::vector<std::unique_ptr<fin::Statement>> > ();
        break;

      case symbol_kind::S_inheritance_opt: // inheritance_opt
      case symbol_kind::S_type_list: // type_list
        yylhs.value.emplace< std::vector<std::unique_ptr<fin::TypeNode>> > ();
        break;

      default:
        break;
    }


      // Default location.
      {
        stack_type::slice range (yystack_, yylen);
        YYLLOC_DEFAULT (yylhs.location, range, yylen);
        yyerror_range[1].location = yylhs.location;
      }

      // Perform the reduction.
      YY_REDUCE_PRINT (yyn);
#if YY_EXCEPTIONS
      try
#endif // YY_EXCEPTIONS
        {
          switch (yyn)
            {
  case 2: // program: statements
#line 205 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               { 
        yylhs.value.as < std::unique_ptr<fin::Program> > () = std::make_unique<fin::Program>(std::move(yystack_[0].value.as < std::vector<std::unique_ptr<fin::Statement>> > ())); 
        yylhs.value.as < std::unique_ptr<fin::Program> > ()->setLoc(yylhs.location);
        fin::root = std::move(yylhs.value.as < std::unique_ptr<fin::Program> > ());
    }
#line 1437 "parser.tab.c"
    break;

  case 3: // program: %empty
#line 210 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             {
        yylhs.value.as < std::unique_ptr<fin::Program> > () = std::make_unique<fin::Program>(std::vector<std::unique_ptr<fin::Statement>>());
        yylhs.value.as < std::unique_ptr<fin::Program> > ()->setLoc(yylhs.location);
        fin::root = std::move(yylhs.value.as < std::unique_ptr<fin::Program> > ());
    }
#line 1447 "parser.tab.c"
    break;

  case 4: // statements: statements statement
#line 218 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                         {
        if (yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()) yystack_[1].value.as < std::vector<std::unique_ptr<fin::Statement>> > ().push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()));
        yylhs.value.as < std::vector<std::unique_ptr<fin::Statement>> > () = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Statement>> > ());
    }
#line 1456 "parser.tab.c"
    break;

  case 5: // statements: statement
#line 222 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                {
        std::vector<std::unique_ptr<fin::Statement>> vec;
        if (yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()) vec.push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()));
        yylhs.value.as < std::vector<std::unique_ptr<fin::Statement>> > () = std::move(vec);
    }
#line 1466 "parser.tab.c"
    break;

  case 6: // statement: annotated_declaration
#line 230 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                            { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1472 "parser.tab.c"
    break;

  case 7: // statement: declaration_with_vis
#line 231 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1478 "parser.tab.c"
    break;

  case 8: // statement: bare_declaration
#line 232 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1484 "parser.tab.c"
    break;

  case 9: // statement: define_declaration
#line 233 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1490 "parser.tab.c"
    break;

  case 10: // statement: macro_declaration
#line 234 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1496 "parser.tab.c"
    break;

  case 11: // statement: import_statement
#line 235 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1502 "parser.tab.c"
    break;

  case 12: // statement: if_statement
#line 236 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1508 "parser.tab.c"
    break;

  case 13: // statement: while_loop
#line 237 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1514 "parser.tab.c"
    break;

  case 14: // statement: for_loop
#line 238 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1520 "parser.tab.c"
    break;

  case 15: // statement: foreach_loop
#line 239 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1526 "parser.tab.c"
    break;

  case 16: // statement: control_statement
#line 240 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1532 "parser.tab.c"
    break;

  case 17: // statement: delete_statement
#line 241 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1538 "parser.tab.c"
    break;

  case 18: // statement: try_catch_statement
#line 242 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1544 "parser.tab.c"
    break;

  case 19: // statement: blame_statement
#line 243 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1550 "parser.tab.c"
    break;

  case 20: // statement: return_statement
#line 244 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1556 "parser.tab.c"
    break;

  case 21: // statement: expression_statement
#line 245 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1562 "parser.tab.c"
    break;

  case 22: // statement: special_declaration
#line 246 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1568 "parser.tab.c"
    break;

  case 23: // statement: implements_block
#line 247 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1574 "parser.tab.c"
    break;

  case 24: // statement: SEMICOLON
#line 248 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::unique_ptr<fin::Statement> > () = nullptr; }
#line 1580 "parser.tab.c"
    break;

  case 25: // block: LBRACE block_stmts RBRACE
#line 252 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                              {
        yylhs.value.as < std::unique_ptr<fin::Block> > () = std::make_unique<fin::Block>(std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Statement>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Block> > ()->setLoc(yylhs.location);
    }
#line 1589 "parser.tab.c"
    break;

  case 26: // block_stmts: statements
#line 259 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               { yylhs.value.as < std::vector<std::unique_ptr<fin::Statement>> > () = std::move(yystack_[0].value.as < std::vector<std::unique_ptr<fin::Statement>> > ()); }
#line 1595 "parser.tab.c"
    break;

  case 27: // block_stmts: %empty
#line 260 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::vector<std::unique_ptr<fin::Statement>> > () = std::vector<std::unique_ptr<fin::Statement>>(); }
#line 1601 "parser.tab.c"
    break;

  case 28: // annotated_declaration: attribute_list visibility_opt declaration_body
#line 266 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                   {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ());
        if (auto* func = dynamic_cast<fin::FunctionDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) {
            func->attributes = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
            func->is_public = yystack_[1].value.as < bool > ();
        } else if (auto* st = dynamic_cast<fin::StructDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) {
            st->attributes = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
            st->is_public = yystack_[1].value.as < bool > ();
        } else if (auto* en = dynamic_cast<fin::EnumDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) {
            en->attributes = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
            en->is_public = yystack_[1].value.as < bool > ();
        } else if (auto* in = dynamic_cast<fin::InterfaceDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) {
            in->attributes = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
            in->is_public = yystack_[1].value.as < bool > ();
        } else if (auto* var = dynamic_cast<fin::VariableDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) {
            var->attributes = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
            var->is_public = yystack_[1].value.as < bool > ();
        } else if (auto* td = dynamic_cast<fin::TypeDefinition*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) {
            td->attributes = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
            td->is_public = yystack_[1].value.as < bool > ();
        }
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 1629 "parser.tab.c"
    break;

  case 29: // declaration_with_vis: KW_PUB declaration_body
#line 292 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                            {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ());
        if (auto* func = dynamic_cast<fin::FunctionDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) func->is_public = true;
        else if (auto* st = dynamic_cast<fin::StructDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) st->is_public = true;
        else if (auto* en = dynamic_cast<fin::EnumDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) en->is_public = true;
        else if (auto* in = dynamic_cast<fin::InterfaceDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) in->is_public = true;
        else if (auto* var = dynamic_cast<fin::VariableDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) var->is_public = true;
        else if (auto* td = dynamic_cast<fin::TypeDefinition*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) td->is_public = true;
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 1644 "parser.tab.c"
    break;

  case 30: // declaration_with_vis: KW_PRIV declaration_body
#line 302 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                               {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ());
        if (auto* func = dynamic_cast<fin::FunctionDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) func->is_public = false;
        else if (auto* st = dynamic_cast<fin::StructDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) st->is_public = false;
        else if (auto* en = dynamic_cast<fin::EnumDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) en->is_public = false;
        else if (auto* in = dynamic_cast<fin::InterfaceDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) in->is_public = false;
        else if (auto* var = dynamic_cast<fin::VariableDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) var->is_public = false;
        else if (auto* td = dynamic_cast<fin::TypeDefinition*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())) td->is_public = false;
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 1659 "parser.tab.c"
    break;

  case 31: // bare_declaration: declaration_body
#line 315 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                     { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1665 "parser.tab.c"
    break;

  case 32: // declaration_body: KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block
#line 319 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                               {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::FunctionDeclaration>(yystack_[8].value.as < std::string > (), std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        static_cast<fin::FunctionDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())->generic_params = std::move(yystack_[7].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
    }
#line 1674 "parser.tab.c"
    break;

  case 33: // declaration_body: KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT SEMICOLON
#line 323 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                     {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::FunctionDeclaration>(yystack_[8].value.as < std::string > (), std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), nullptr);
        static_cast<fin::FunctionDeclaration*>(yylhs.value.as < std::unique_ptr<fin::Statement> > ().get())->generic_params = std::move(yystack_[7].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
    }
#line 1683 "parser.tab.c"
    break;

  case 34: // declaration_body: KW_STRUCT IDENTIFIER generic_params_opt inheritance_opt LBRACE struct_body_content RBRACE
#line 327 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                                {
        yystack_[1].value.as < std::unique_ptr<fin::StructDeclaration> > ()->name = yystack_[5].value.as < std::string > ();
        yystack_[1].value.as < std::unique_ptr<fin::StructDeclaration> > ()->generic_params = std::move(yystack_[4].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        yystack_[1].value.as < std::unique_ptr<fin::StructDeclaration> > ()->parents = std::move(yystack_[3].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[1].value.as < std::unique_ptr<fin::StructDeclaration> > ());
    }
#line 1694 "parser.tab.c"
    break;

  case 35: // declaration_body: KW_INTERFACE IDENTIFIER generic_params_opt LBRACE interface_body_content RBRACE
#line 333 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                      {
        yystack_[1].value.as < std::unique_ptr<fin::InterfaceDeclaration> > ()->name = yystack_[4].value.as < std::string > ();
        yystack_[1].value.as < std::unique_ptr<fin::InterfaceDeclaration> > ()->generic_params = std::move(yystack_[3].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[1].value.as < std::unique_ptr<fin::InterfaceDeclaration> > ());
    }
#line 1704 "parser.tab.c"
    break;

  case 36: // declaration_body: KW_ENUM IDENTIFIER LBRACE enum_values RBRACE
#line 338 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                   {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::EnumDeclaration>(yystack_[3].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()), false);
    }
#line 1712 "parser.tab.c"
    break;

  case 37: // declaration_body: KW_CLASS IDENTIFIER generic_params_opt inheritance_opt LBRACE struct_body_content RBRACE
#line 341 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                               {
        auto cls = std::make_unique<fin::ClassDeclaration>(yystack_[5].value.as < std::string > (), std::move(yystack_[1].value.as < std::unique_ptr<fin::StructDeclaration> > ()->members), false);
        cls->methods = std::move(yystack_[1].value.as < std::unique_ptr<fin::StructDeclaration> > ()->methods);
        cls->operators = std::move(yystack_[1].value.as < std::unique_ptr<fin::StructDeclaration> > ()->operators);
        cls->constructors = std::move(yystack_[1].value.as < std::unique_ptr<fin::StructDeclaration> > ()->constructors);
        cls->destructor = std::move(yystack_[1].value.as < std::unique_ptr<fin::StructDeclaration> > ()->destructor);
        cls->attributes = std::move(yystack_[1].value.as < std::unique_ptr<fin::StructDeclaration> > ()->attributes);
        cls->generic_params = std::move(yystack_[4].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        cls->parents = std::move(yystack_[3].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(cls);
    }
#line 1728 "parser.tab.c"
    break;

  case 38: // declaration_body: KW_LET IDENTIFIER LT type GT EQUAL expression SEMICOLON
#line 352 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                              {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::VariableDeclaration>(true, yystack_[6].value.as < std::string > (), std::move(yystack_[4].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()));
    }
#line 1736 "parser.tab.c"
    break;

  case 39: // declaration_body: KW_CONST IDENTIFIER LT type GT EQUAL expression SEMICOLON
#line 355 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::VariableDeclaration>(false, yystack_[6].value.as < std::string > (), std::move(yystack_[4].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()));
    }
#line 1744 "parser.tab.c"
    break;

  case 40: // declaration_body: KW_LET IDENTIFIER LT type GT SEMICOLON
#line 358 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                             {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::VariableDeclaration>(true, yystack_[4].value.as < std::string > (), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), nullptr);
    }
#line 1752 "parser.tab.c"
    break;

  case 41: // declaration_body: type_definition
#line 361 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                      { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 1758 "parser.tab.c"
    break;

  case 42: // attributes_opt: attribute_list
#line 367 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                   { yylhs.value.as < std::vector<std::unique_ptr<fin::Attribute>> > () = std::move(yystack_[0].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ()); }
#line 1764 "parser.tab.c"
    break;

  case 43: // attributes_opt: %empty
#line 368 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::vector<std::unique_ptr<fin::Attribute>> > () = std::vector<std::unique_ptr<fin::Attribute>>(); }
#line 1770 "parser.tab.c"
    break;

  case 44: // attribute_list: attribute_list attribute
#line 372 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yystack_[1].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ().push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::Attribute> > ())); yylhs.value.as < std::vector<std::unique_ptr<fin::Attribute>> > () = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ()); }
#line 1776 "parser.tab.c"
    break;

  case 45: // attribute_list: attribute
#line 373 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                { std::vector<std::unique_ptr<fin::Attribute>> v; v.push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::Attribute> > ())); yylhs.value.as < std::vector<std::unique_ptr<fin::Attribute>> > () = std::move(v); }
#line 1782 "parser.tab.c"
    break;

  case 46: // attribute: HASH LBRACKET attr_id EQUAL STRING_LITERAL RBRACKET
#line 377 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                        {
        yylhs.value.as < std::unique_ptr<fin::Attribute> > () = std::make_unique<fin::Attribute>(yystack_[3].value.as < std::string > (), yystack_[1].value.as < std::string > ());
        yylhs.value.as < std::unique_ptr<fin::Attribute> > ()->setLoc(yylhs.location);
    }
#line 1791 "parser.tab.c"
    break;

  case 47: // attribute: HASH LBRACKET attr_id RBRACKET
#line 381 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                     {
        yylhs.value.as < std::unique_ptr<fin::Attribute> > () = std::make_unique<fin::Attribute>(yystack_[1].value.as < std::string > (), true);
        yylhs.value.as < std::unique_ptr<fin::Attribute> > ()->setLoc(yylhs.location);
    }
#line 1800 "parser.tab.c"
    break;

  case 48: // attribute: HASH LBRACKET attr_id LPAREN dotted_path RPAREN RBRACKET
#line 385 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                               {
        yylhs.value.as < std::unique_ptr<fin::Attribute> > () = std::make_unique<fin::Attribute>(yystack_[4].value.as < std::string > (), yystack_[2].value.as < std::string > ());
        yylhs.value.as < std::unique_ptr<fin::Attribute> > ()->setLoc(yylhs.location);
    }
#line 1809 "parser.tab.c"
    break;

  case 49: // attr_id: IDENTIFIER
#line 391 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
         { yylhs.value.as < std::string > () = yystack_[0].value.as < std::string > (); }
#line 1815 "parser.tab.c"
    break;

  case 50: // attr_id: KW_CLASS
#line 391 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                               { yylhs.value.as < std::string > () = "class"; }
#line 1821 "parser.tab.c"
    break;

  case 51: // generic_params_opt: LT generic_param_list GT
#line 396 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                             { yylhs.value.as < std::vector<std::unique_ptr<fin::GenericParam>> > () = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ()); }
#line 1827 "parser.tab.c"
    break;

  case 52: // generic_params_opt: %empty
#line 397 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::vector<std::unique_ptr<fin::GenericParam>> > () = std::vector<std::unique_ptr<fin::GenericParam>>(); }
#line 1833 "parser.tab.c"
    break;

  case 53: // generic_param_list: generic_param_list COMMA generic_param
#line 401 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                           { yystack_[2].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ().push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::GenericParam> > ())); yylhs.value.as < std::vector<std::unique_ptr<fin::GenericParam>> > () = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ()); }
#line 1839 "parser.tab.c"
    break;

  case 54: // generic_param_list: generic_param
#line 402 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                    { std::vector<std::unique_ptr<fin::GenericParam>> v; v.push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::GenericParam> > ())); yylhs.value.as < std::vector<std::unique_ptr<fin::GenericParam>> > () = std::move(v); }
#line 1845 "parser.tab.c"
    break;

  case 55: // generic_param: IDENTIFIER
#line 406 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               { 
        yylhs.value.as < std::unique_ptr<fin::GenericParam> > () = std::make_unique<fin::GenericParam>(yystack_[0].value.as < std::string > ()); 
        yylhs.value.as < std::unique_ptr<fin::GenericParam> > ()->setLoc(yylhs.location);
    }
#line 1854 "parser.tab.c"
    break;

  case 56: // generic_param: IDENTIFIER COLON type
#line 410 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                            {
        yylhs.value.as < std::unique_ptr<fin::GenericParam> > () = std::make_unique<fin::GenericParam>(yystack_[2].value.as < std::string > (), std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()));
        yylhs.value.as < std::unique_ptr<fin::GenericParam> > ()->setLoc(yylhs.location);
    }
#line 1863 "parser.tab.c"
    break;

  case 57: // import_statement: KW_IMPORT STRING_LITERAL SEMICOLON
#line 420 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                       {
        std::vector<std::string> empty;
        // Strip quotes from string literal if present
        std::string src = yystack_[1].value.as < std::string > ();
        if (src.size() >= 2 && src.front() == '"' && src.back() == '"') {
            src = src.substr(1, src.size() - 2);
        }
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::ImportModule>(src, false, "", empty);
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 1878 "parser.tab.c"
    break;

  case 58: // import_statement: KW_IMPORT dotted_path SEMICOLON
#line 431 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                      {
        std::vector<std::string> empty;
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::ImportModule>(yystack_[1].value.as < std::string > (), true, "", empty);
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 1888 "parser.tab.c"
    break;

  case 59: // import_statement: KW_IMPORT dotted_path KW_AS IDENTIFIER SEMICOLON
#line 437 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                       {
        std::vector<std::string> empty;
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::ImportModule>(yystack_[3].value.as < std::string > (), true, yystack_[1].value.as < std::string > (), empty);
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 1898 "parser.tab.c"
    break;

  case 60: // import_statement: KW_IMPORT LBRACE import_list RBRACE KW_FROM STRING_LITERAL SEMICOLON
#line 443 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                           {
        std::string src = yystack_[1].value.as < std::string > ();
        if (src.size() >= 2 && src.front() == '"' && src.back() == '"') {
            src = src.substr(1, src.size() - 2);
        }
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::ImportModule>(src, false, "", yystack_[4].value.as < std::vector<std::string> > ());
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 1911 "parser.tab.c"
    break;

  case 61: // import_statement: KW_IMPORT LBRACE import_list RBRACE KW_FROM dotted_path SEMICOLON
#line 452 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                        {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::ImportModule>(yystack_[1].value.as < std::string > (), true, "", yystack_[4].value.as < std::vector<std::string> > ());
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 1920 "parser.tab.c"
    break;

  case 62: // import_list: import_list COMMA IDENTIFIER
#line 458 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                 { yystack_[2].value.as < std::vector<std::string> > ().push_back(yystack_[0].value.as < std::string > ()); yylhs.value.as < std::vector<std::string> > () = std::move(yystack_[2].value.as < std::vector<std::string> > ()); }
#line 1926 "parser.tab.c"
    break;

  case 63: // import_list: IDENTIFIER
#line 459 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                 { std::vector<std::string> v; v.push_back(yystack_[0].value.as < std::string > ()); yylhs.value.as < std::vector<std::string> > () = std::move(v); }
#line 1932 "parser.tab.c"
    break;

  case 64: // dotted_path: IDENTIFIER
#line 463 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               { yylhs.value.as < std::string > () = yystack_[0].value.as < std::string > (); }
#line 1938 "parser.tab.c"
    break;

  case 65: // dotted_path: dotted_path DOT IDENTIFIER
#line 464 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                 { yylhs.value.as < std::string > () = yystack_[2].value.as < std::string > () + "." + yystack_[0].value.as < std::string > (); }
#line 1944 "parser.tab.c"
    break;

  case 66: // operator_params_opt: LPAREN params RPAREN
#line 468 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                         { yylhs.value.as < std::vector<std::unique_ptr<fin::Parameter>> > () = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()); }
#line 1950 "parser.tab.c"
    break;

  case 67: // operator_params_opt: %empty
#line 469 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::vector<std::unique_ptr<fin::Parameter>> > () = std::vector<std::unique_ptr<fin::Parameter>>(); }
#line 1956 "parser.tab.c"
    break;

  case 68: // inheritance_opt: COLON LT type_list GT
#line 475 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                          { yylhs.value.as < std::vector<std::unique_ptr<fin::TypeNode>> > () = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ()); }
#line 1962 "parser.tab.c"
    break;

  case 69: // inheritance_opt: %empty
#line 476 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::vector<std::unique_ptr<fin::TypeNode>> > () = std::vector<std::unique_ptr<fin::TypeNode>>(); }
#line 1968 "parser.tab.c"
    break;

  case 70: // struct_body_content: struct_body_content attributes_opt visibility_opt struct_item_rest
#line 480 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                         {
        if (auto* member = dynamic_cast<fin::StructMember*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            member->attributes = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
            member->is_public = yystack_[1].value.as < bool > ();
            yystack_[3].value.as < std::unique_ptr<fin::StructDeclaration> > ()->members.push_back(std::unique_ptr<fin::StructMember>(static_cast<fin::StructMember*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release())));
        }
        else if (auto* func = dynamic_cast<fin::FunctionDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            func->attributes = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
            func->is_public = yystack_[1].value.as < bool > ();
            yystack_[3].value.as < std::unique_ptr<fin::StructDeclaration> > ()->methods.push_back(std::unique_ptr<fin::FunctionDeclaration>(static_cast<fin::FunctionDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release())));
        }
        else if (auto* op = dynamic_cast<fin::OperatorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            op->is_public = yystack_[1].value.as < bool > ();
            yystack_[3].value.as < std::unique_ptr<fin::StructDeclaration> > ()->operators.push_back(std::unique_ptr<fin::OperatorDeclaration>(static_cast<fin::OperatorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release())));
        }
        else if (auto* ctor = dynamic_cast<fin::ConstructorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            yystack_[3].value.as < std::unique_ptr<fin::StructDeclaration> > ()->constructors.push_back(std::unique_ptr<fin::ConstructorDeclaration>(static_cast<fin::ConstructorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release())));
        }
        else if (auto* dtor = dynamic_cast<fin::DestructorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            yystack_[3].value.as < std::unique_ptr<fin::StructDeclaration> > ()->destructor = std::unique_ptr<fin::DestructorDeclaration>(static_cast<fin::DestructorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release()));
        }
        yylhs.value.as < std::unique_ptr<fin::StructDeclaration> > () = std::move(yystack_[3].value.as < std::unique_ptr<fin::StructDeclaration> > ());
    }
#line 1996 "parser.tab.c"
    break;

  case 71: // struct_body_content: %empty
#line 503 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { 
        std::vector<std::unique_ptr<fin::StructMember>> m;
        yylhs.value.as < std::unique_ptr<fin::StructDeclaration> > () = std::make_unique<fin::StructDeclaration>("", std::move(m), false); 
    }
#line 2005 "parser.tab.c"
    break;

  case 72: // struct_item_rest: IDENTIFIER LT type GT COMMA
#line 512 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::StructMember>(yystack_[4].value.as < std::string > (), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), false);
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2014 "parser.tab.c"
    break;

  case 73: // struct_item_rest: IDENTIFIER LT type GT
#line 516 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                            {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::StructMember>(yystack_[3].value.as < std::string > (), std::move(yystack_[1].value.as < std::unique_ptr<fin::TypeNode> > ()), false);
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2023 "parser.tab.c"
    break;

  case 74: // struct_item_rest: IDENTIFIER LT type GT EQUAL expression COMMA
#line 520 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                   {
        auto member = std::make_unique<fin::StructMember>(yystack_[6].value.as < std::string > (), std::move(yystack_[4].value.as < std::unique_ptr<fin::TypeNode> > ()), false);
        member->default_value = std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ());
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::move(member);
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2034 "parser.tab.c"
    break;

  case 75: // struct_item_rest: IDENTIFIER LT type GT EQUAL expression
#line 526 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                             {
        auto member = std::make_unique<fin::StructMember>(yystack_[5].value.as < std::string > (), std::move(yystack_[3].value.as < std::unique_ptr<fin::TypeNode> > ()), false);
        member->default_value = std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ());
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::move(member);
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2045 "parser.tab.c"
    break;

  case 76: // struct_item_rest: KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block
#line 533 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                 {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::FunctionDeclaration>(yystack_[8].value.as < std::string > (), std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        static_cast<fin::FunctionDeclaration*>(yylhs.value.as < std::unique_ptr<fin::ASTNode> > ().get())->generic_params = std::move(yystack_[7].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2055 "parser.tab.c"
    break;

  case 77: // struct_item_rest: KW_STATIC KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block
#line 539 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                           {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::FunctionDeclaration>(yystack_[8].value.as < std::string > (), std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        auto* func = static_cast<fin::FunctionDeclaration*>(yylhs.value.as < std::unique_ptr<fin::ASTNode> > ().get());
        func->generic_params = std::move(yystack_[7].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        func->is_static = true;
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2067 "parser.tab.c"
    break;

  case 78: // struct_item_rest: KW_OPERATOR operator_symbol operator_generics_opt operator_params_opt implements_opt LT type GT block
#line 547 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                                            {
        auto op = std::make_unique<fin::OperatorDeclaration>(yystack_[7].value.as < fin::ASTTokenKind > (), std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()), false);
        op->generic_params = std::move(yystack_[6].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        op->implements_type = std::move(yystack_[4].value.as < std::unique_ptr<fin::TypeNode> > ());
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::move(op);
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2079 "parser.tab.c"
    break;

  case 79: // struct_item_rest: KW_OPERATOR operator_symbol operator_generics_opt operator_params_opt implements_opt LT type GT SEMICOLON
#line 554 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                                                {
        auto op = std::make_unique<fin::OperatorDeclaration>(yystack_[7].value.as < fin::ASTTokenKind > (), std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), nullptr, false);
        op->generic_params = std::move(yystack_[6].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        op->implements_type = std::move(yystack_[4].value.as < std::unique_ptr<fin::TypeNode> > ());
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::move(op);
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2091 "parser.tab.c"
    break;

  case 80: // struct_item_rest: IDENTIFIER LPAREN params RPAREN block
#line 562 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                            {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::ConstructorDeclaration>(yystack_[4].value.as < std::string > (), std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2100 "parser.tab.c"
    break;

  case 81: // struct_item_rest: IDENTIFIER LPAREN params RPAREN LT type GT block
#line 566 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                       {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::ConstructorDeclaration>(yystack_[7].value.as < std::string > (), std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()));
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2109 "parser.tab.c"
    break;

  case 82: // struct_item_rest: TILDE IDENTIFIER LPAREN RPAREN block
#line 571 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                           {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::DestructorDeclaration>(yystack_[3].value.as < std::string > (), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2118 "parser.tab.c"
    break;

  case 83: // operator_symbol: PLUS
#line 578 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
         { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::PLUS; }
#line 2124 "parser.tab.c"
    break;

  case 84: // operator_symbol: MINUS
#line 579 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
            { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::MINUS; }
#line 2130 "parser.tab.c"
    break;

  case 85: // operator_symbol: MULT
#line 580 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
           { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::MULT; }
#line 2136 "parser.tab.c"
    break;

  case 86: // operator_symbol: DIV
#line 581 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
          { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::DIV; }
#line 2142 "parser.tab.c"
    break;

  case 87: // operator_symbol: MOD
#line 582 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
          { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::MOD; }
#line 2148 "parser.tab.c"
    break;

  case 88: // operator_symbol: EQEQ
#line 583 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
           { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::EQEQ; }
#line 2154 "parser.tab.c"
    break;

  case 89: // operator_symbol: NOTEQ
#line 584 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
            { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::NOTEQ; }
#line 2160 "parser.tab.c"
    break;

  case 90: // operator_symbol: LT
#line 585 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
         { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::LT; }
#line 2166 "parser.tab.c"
    break;

  case 91: // operator_symbol: GT
#line 586 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
         { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::GT; }
#line 2172 "parser.tab.c"
    break;

  case 92: // operator_symbol: LTEQ
#line 587 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
           { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::LTEQ; }
#line 2178 "parser.tab.c"
    break;

  case 93: // operator_symbol: GTEQ
#line 588 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
           { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::GTEQ; }
#line 2184 "parser.tab.c"
    break;

  case 94: // operator_symbol: AMPERSAND
#line 589 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::AMPERSAND; }
#line 2190 "parser.tab.c"
    break;

  case 95: // operator_symbol: AND
#line 590 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
          { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::AND; }
#line 2196 "parser.tab.c"
    break;

  case 96: // operator_symbol: PIPE
#line 591 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
           { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::PIPE; }
#line 2202 "parser.tab.c"
    break;

  case 97: // operator_symbol: OR
#line 592 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
         { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::OR; }
#line 2208 "parser.tab.c"
    break;

  case 98: // operator_symbol: CARET
#line 593 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
            { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::CARET; }
#line 2214 "parser.tab.c"
    break;

  case 99: // operator_symbol: SHIFTLEFT
#line 594 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::SHIFTLEFT; }
#line 2220 "parser.tab.c"
    break;

  case 100: // operator_symbol: SHIFTRIGHT
#line 595 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                 { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::SHIFTRIGHT; }
#line 2226 "parser.tab.c"
    break;

  case 101: // operator_symbol: SHIFTLEFTEQUAL
#line 596 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                     { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::SHIFTLEFTEQUAL; }
#line 2232 "parser.tab.c"
    break;

  case 102: // operator_symbol: SHIFTRIGHTEQUAL
#line 597 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                      { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::SHIFTRIGHTEQUAL; }
#line 2238 "parser.tab.c"
    break;

  case 103: // operator_symbol: NOT
#line 598 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
          { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::NOT; }
#line 2244 "parser.tab.c"
    break;

  case 104: // operator_symbol: EQUAL
#line 599 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
            { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::EQUAL; }
#line 2250 "parser.tab.c"
    break;

  case 105: // operator_symbol: LBRACKET RBRACKET
#line 600 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                        { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::INDEX; }
#line 2256 "parser.tab.c"
    break;

  case 106: // operator_symbol: LBRACKET RBRACKET EQUAL
#line 601 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                              { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::INDEX_ASSIGN; }
#line 2262 "parser.tab.c"
    break;

  case 107: // operator_symbol: BACKTICK MULT
#line 602 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                    { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::DEREF; }
#line 2268 "parser.tab.c"
    break;

  case 108: // operator_symbol: BACKTICK MINUS
#line 603 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                     { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::UNARY_MINUS; }
#line 2274 "parser.tab.c"
    break;

  case 109: // operator_symbol: LPAREN ELLIPSIS IDENTIFIER RPAREN
#line 604 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                        { yylhs.value.as < fin::ASTTokenKind > () = fin::ASTTokenKind::VARIADIC_CALL; }
#line 2280 "parser.tab.c"
    break;

  case 110: // operator_generics_opt: COLON LT generic_param_list GT
#line 608 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                   { yylhs.value.as < std::vector<std::unique_ptr<fin::GenericParam>> > () = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ()); }
#line 2286 "parser.tab.c"
    break;

  case 111: // operator_generics_opt: %empty
#line 609 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::vector<std::unique_ptr<fin::GenericParam>> > () = std::vector<std::unique_ptr<fin::GenericParam>>(); }
#line 2292 "parser.tab.c"
    break;

  case 112: // implements_opt: KW_IMPLEMENTS LT type GT
#line 613 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                               { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::move(yystack_[1].value.as < std::unique_ptr<fin::TypeNode> > ()); }
#line 2298 "parser.tab.c"
    break;

  case 113: // implements_opt: %empty
#line 614 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = nullptr; }
#line 2304 "parser.tab.c"
    break;

  case 114: // interface_body_content: interface_body_content attributes_opt visibility_opt interface_item_rest
#line 620 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                             {
        if (auto* member = dynamic_cast<fin::StructMember*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            member->attributes = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
            member->is_public = yystack_[1].value.as < bool > ();
            yystack_[3].value.as < std::unique_ptr<fin::InterfaceDeclaration> > ()->members.push_back(std::unique_ptr<fin::StructMember>(static_cast<fin::StructMember*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release())));
        }
        else if (auto* func = dynamic_cast<fin::FunctionDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            func->attributes = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
            func->is_public = yystack_[1].value.as < bool > ();
            yystack_[3].value.as < std::unique_ptr<fin::InterfaceDeclaration> > ()->methods.push_back(std::unique_ptr<fin::FunctionDeclaration>(static_cast<fin::FunctionDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release())));
        }
        else if (auto* op = dynamic_cast<fin::OperatorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            op->is_public = yystack_[1].value.as < bool > ();
            yystack_[3].value.as < std::unique_ptr<fin::InterfaceDeclaration> > ()->operators.push_back(std::unique_ptr<fin::OperatorDeclaration>(static_cast<fin::OperatorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release())));
        }
        else if (auto* ctor = dynamic_cast<fin::ConstructorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            yystack_[3].value.as < std::unique_ptr<fin::InterfaceDeclaration> > ()->constructors.push_back(std::unique_ptr<fin::ConstructorDeclaration>(static_cast<fin::ConstructorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release())));
        }
        else if (auto* dtor = dynamic_cast<fin::DestructorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            yystack_[3].value.as < std::unique_ptr<fin::InterfaceDeclaration> > ()->destructor = std::unique_ptr<fin::DestructorDeclaration>(static_cast<fin::DestructorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release()));
        }
        yylhs.value.as < std::unique_ptr<fin::InterfaceDeclaration> > () = std::move(yystack_[3].value.as < std::unique_ptr<fin::InterfaceDeclaration> > ());
    }
#line 2332 "parser.tab.c"
    break;

  case 115: // interface_body_content: %empty
#line 643 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { 
        std::vector<std::unique_ptr<fin::StructMember>> m;
        std::vector<std::unique_ptr<fin::FunctionDeclaration>> f;
        std::vector<std::unique_ptr<fin::OperatorDeclaration>> o;
        std::vector<std::unique_ptr<fin::ConstructorDeclaration>> c;
        std::unique_ptr<fin::DestructorDeclaration> d = nullptr;
        yylhs.value.as < std::unique_ptr<fin::InterfaceDeclaration> > () = std::make_unique<fin::InterfaceDeclaration>("", std::move(m), std::move(f), std::move(o), std::move(c), std::move(d), false); 
    }
#line 2345 "parser.tab.c"
    break;

  case 116: // interface_item_rest: IDENTIFIER LT type GT SEMICOLON
#line 655 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                    {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::StructMember>(yystack_[4].value.as < std::string > (), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), false);
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2354 "parser.tab.c"
    break;

  case 117: // interface_item_rest: declaration_body
#line 660 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                       { yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Statement> > ()); }
#line 2360 "parser.tab.c"
    break;

  case 118: // interface_item_rest: KW_OPERATOR operator_symbol operator_generics_opt operator_params_opt LT type GT SEMICOLON
#line 663 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                                 {
        auto op = std::make_unique<fin::OperatorDeclaration>(yystack_[6].value.as < fin::ASTTokenKind > (), std::move(yystack_[4].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), nullptr, false);
        op->generic_params = std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::move(op);
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2371 "parser.tab.c"
    break;

  case 119: // interface_item_rest: KW_SELF_TYPE LPAREN params RPAREN SEMICOLON
#line 671 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                  {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::ConstructorDeclaration>("Self", std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), nullptr);
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2380 "parser.tab.c"
    break;

  case 120: // interface_item_rest: TILDE KW_SELF_TYPE LPAREN RPAREN SEMICOLON
#line 677 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                 {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::DestructorDeclaration>("Self", nullptr);
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2389 "parser.tab.c"
    break;

  case 121: // params: param_list
#line 686 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               { yylhs.value.as < std::vector<std::unique_ptr<fin::Parameter>> > () = std::move(yystack_[0].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()); }
#line 2395 "parser.tab.c"
    break;

  case 122: // params: %empty
#line 687 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::vector<std::unique_ptr<fin::Parameter>> > () = std::vector<std::unique_ptr<fin::Parameter>>(); }
#line 2401 "parser.tab.c"
    break;

  case 123: // param_list: param_list COMMA param
#line 691 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                           { yystack_[2].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ().push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::Parameter> > ())); yylhs.value.as < std::vector<std::unique_ptr<fin::Parameter>> > () = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()); }
#line 2407 "parser.tab.c"
    break;

  case 124: // param_list: param
#line 692 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
            { std::vector<std::unique_ptr<fin::Parameter>> v; v.push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::Parameter> > ())); yylhs.value.as < std::vector<std::unique_ptr<fin::Parameter>> > () = std::move(v); }
#line 2413 "parser.tab.c"
    break;

  case 125: // param: IDENTIFIER COLON type
#line 696 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                          {
        yylhs.value.as < std::unique_ptr<fin::Parameter> > () = std::make_unique<fin::Parameter>(yystack_[2].value.as < std::string > (), std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()), nullptr, false);
        yylhs.value.as < std::unique_ptr<fin::Parameter> > ()->setLoc(yylhs.location);
    }
#line 2422 "parser.tab.c"
    break;

  case 126: // param: ELLIPSIS IDENTIFIER COLON type
#line 700 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                     {
        yylhs.value.as < std::unique_ptr<fin::Parameter> > () = std::make_unique<fin::Parameter>(yystack_[2].value.as < std::string > (), std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()), nullptr, true);
        yylhs.value.as < std::unique_ptr<fin::Parameter> > ()->setLoc(yylhs.location);
    }
#line 2431 "parser.tab.c"
    break;

  case 127: // super_expression: KW_SUPER LBRACE field_assignments RBRACE
#line 710 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                             {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::SuperExpression>(std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 2440 "parser.tab.c"
    break;

  case 128: // super_expression: KW_SUPER DOUBLE_COLON IDENTIFIER LBRACE field_assignments RBRACE
#line 715 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                       {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::SuperExpression>(yystack_[3].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 2449 "parser.tab.c"
    break;

  case 129: // super_expression: KW_SUPER DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN
#line 720 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                               {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::SuperExpression>(yystack_[3].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 2458 "parser.tab.c"
    break;

  case 130: // super_expression: KW_SUPER LPAREN arguments RPAREN
#line 725 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                       {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::SuperExpression>("", std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 2467 "parser.tab.c"
    break;

  case 131: // define_declaration: AT KW_DEFINE IDENTIFIER LPAREN extern_params RPAREN LT type GT SEMICOLON
#line 734 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                             {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::DefineDeclaration>(yystack_[7].value.as < std::string > (), std::move(yystack_[5].value.as < std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > ().first), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), yystack_[5].value.as < std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > ().second);
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 2476 "parser.tab.c"
    break;

  case 132: // extern_params: param_list COMMA ELLIPSIS
#line 741 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                              { yylhs.value.as < std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > () = std::make_pair(std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), true); }
#line 2482 "parser.tab.c"
    break;

  case 133: // extern_params: param_list
#line 742 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                 { yylhs.value.as < std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > () = std::make_pair(std::move(yystack_[0].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), false); }
#line 2488 "parser.tab.c"
    break;

  case 134: // extern_params: ELLIPSIS
#line 743 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               { yylhs.value.as < std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > () = std::make_pair(std::vector<std::unique_ptr<fin::Parameter>>(), true); }
#line 2494 "parser.tab.c"
    break;

  case 135: // extern_params: %empty
#line 744 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > () = std::make_pair(std::vector<std::unique_ptr<fin::Parameter>>(), false); }
#line 2500 "parser.tab.c"
    break;

  case 136: // macro_declaration: AT KW_MACRO IDENTIFIER LPAREN macro_param_list RPAREN block
#line 750 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::MacroDeclaration>(yystack_[4].value.as < std::string > (), std::move(yystack_[2].value.as < std::vector<fin::MacroParam> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 2509 "parser.tab.c"
    break;

  case 137: // macro_declaration: KW_MACRO IDENTIFIER LBRACE macro_rules RBRACE
#line 754 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                    {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::MacroDeclaration>(yystack_[3].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<fin::MacroRule> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 2518 "parser.tab.c"
    break;

  case 138: // macro_rules: macro_rules macro_rule
#line 761 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                           { yystack_[1].value.as < std::vector<fin::MacroRule> > ().push_back(std::move(yystack_[0].value.as < fin::MacroRule > ())); yylhs.value.as < std::vector<fin::MacroRule> > () = std::move(yystack_[1].value.as < std::vector<fin::MacroRule> > ()); }
#line 2524 "parser.tab.c"
    break;

  case 139: // macro_rules: macro_rule
#line 762 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                 { std::vector<fin::MacroRule> v; v.push_back(std::move(yystack_[0].value.as < fin::MacroRule > ())); yylhs.value.as < std::vector<fin::MacroRule> > () = std::move(v); }
#line 2530 "parser.tab.c"
    break;

  case 140: // macro_rule: LPAREN IDENTIFIER RPAREN ARROW block
#line 766 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                         { 
        yylhs.value.as < fin::MacroRule > () = fin::MacroRule{yystack_[3].value.as < std::string > (), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ())}; 
    }
#line 2538 "parser.tab.c"
    break;

  case 141: // macro_rule: LPAREN STRING_LITERAL RPAREN ARROW block
#line 769 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                               { 
        yylhs.value.as < fin::MacroRule > () = fin::MacroRule{yystack_[3].value.as < std::string > (), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ())}; 
    }
#line 2546 "parser.tab.c"
    break;

  case 142: // macro_rule: LPAREN RPAREN ARROW block
#line 772 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                {
        yylhs.value.as < fin::MacroRule > () = fin::MacroRule{"", std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ())};
    }
#line 2554 "parser.tab.c"
    break;

  case 143: // macro_param_list: macro_param_list COMMA macro_param
#line 778 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                       { yystack_[2].value.as < std::vector<fin::MacroParam> > ().push_back(yystack_[0].value.as < fin::MacroParam > ()); yylhs.value.as < std::vector<fin::MacroParam> > () = std::move(yystack_[2].value.as < std::vector<fin::MacroParam> > ()); }
#line 2560 "parser.tab.c"
    break;

  case 144: // macro_param_list: macro_param
#line 779 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                  { std::vector<fin::MacroParam> v; v.push_back(yystack_[0].value.as < fin::MacroParam > ()); yylhs.value.as < std::vector<fin::MacroParam> > () = std::move(v); }
#line 2566 "parser.tab.c"
    break;

  case 145: // macro_param_list: %empty
#line 780 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::vector<fin::MacroParam> > () = std::vector<fin::MacroParam>(); }
#line 2572 "parser.tab.c"
    break;

  case 146: // macro_param: IDENTIFIER
#line 785 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               { 
        yylhs.value.as < fin::MacroParam > () = fin::MacroParam{yystack_[0].value.as < std::string > (), "expr", false}; 
    }
#line 2580 "parser.tab.c"
    break;

  case 147: // macro_param: IDENTIFIER ELLIPSIS
#line 789 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                          { 
        yylhs.value.as < fin::MacroParam > () = fin::MacroParam{yystack_[1].value.as < std::string > (), "expr", true}; 
    }
#line 2588 "parser.tab.c"
    break;

  case 148: // macro_param: IDENTIFIER COLON IDENTIFIER
#line 793 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                  { 
        yylhs.value.as < fin::MacroParam > () = fin::MacroParam{yystack_[2].value.as < std::string > (), yystack_[0].value.as < std::string > (), false}; 
    }
#line 2596 "parser.tab.c"
    break;

  case 149: // macro_param: IDENTIFIER COLON IDENTIFIER ELLIPSIS
#line 797 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                           { 
        yylhs.value.as < fin::MacroParam > () = fin::MacroParam{yystack_[3].value.as < std::string > (), yystack_[1].value.as < std::string > (), true}; 
    }
#line 2604 "parser.tab.c"
    break;

  case 150: // type_definition: KW_TYPE IDENTIFIER generic_params_opt EQUAL LT type GT SEMICOLON
#line 806 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                     {
        auto td = std::make_unique<fin::TypeDefinition>(yystack_[6].value.as < std::string > (), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()));
        td->generic_params = std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(td);
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 2615 "parser.tab.c"
    break;

  case 151: // type_definition: KW_TYPE IDENTIFIER generic_params_opt EQUAL KW_ANY KW_IMPLEMENTS LT type_list GT SEMICOLON
#line 813 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                                 {
        auto voidType = std::make_unique<fin::TypeNode>("any");
        auto td = std::make_unique<fin::TypeDefinition>(yystack_[8].value.as < std::string > (), std::move(voidType));
        td->generic_params = std::move(yystack_[7].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        td->has_implements = true;
        td->implements_list = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(td);
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 2629 "parser.tab.c"
    break;

  case 152: // implements_block: IDENTIFIER KW_IMPLEMENTS LT type GT LBRACE implements_body_content RBRACE
#line 828 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                              {
        yystack_[1].value.as < std::unique_ptr<fin::ImplementsBlock> > ()->target_type = yystack_[7].value.as < std::string > ();
        yystack_[1].value.as < std::unique_ptr<fin::ImplementsBlock> > ()->interface_type = std::move(yystack_[4].value.as < std::unique_ptr<fin::TypeNode> > ());
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(yystack_[1].value.as < std::unique_ptr<fin::ImplementsBlock> > ());
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 2640 "parser.tab.c"
    break;

  case 153: // implements_body_content: implements_body_content attributes_opt visibility_opt implements_item_rest
#line 837 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                               {
        if (auto* func = dynamic_cast<fin::FunctionDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            func->attributes = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
            func->is_public = yystack_[1].value.as < bool > ();
            yystack_[3].value.as < std::unique_ptr<fin::ImplementsBlock> > ()->methods.push_back(std::unique_ptr<fin::FunctionDeclaration>(static_cast<fin::FunctionDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release())));
        }
        else if (auto* op = dynamic_cast<fin::OperatorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().get())) {
            op->is_public = yystack_[1].value.as < bool > ();
            yystack_[3].value.as < std::unique_ptr<fin::ImplementsBlock> > ()->operators.push_back(std::unique_ptr<fin::OperatorDeclaration>(static_cast<fin::OperatorDeclaration*>(yystack_[0].value.as < std::unique_ptr<fin::ASTNode> > ().release())));
        }
        yylhs.value.as < std::unique_ptr<fin::ImplementsBlock> > () = std::move(yystack_[3].value.as < std::unique_ptr<fin::ImplementsBlock> > ());
    }
#line 2657 "parser.tab.c"
    break;

  case 154: // implements_body_content: %empty
#line 849 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { 
        yylhs.value.as < std::unique_ptr<fin::ImplementsBlock> > () = std::make_unique<fin::ImplementsBlock>("", nullptr);
    }
#line 2665 "parser.tab.c"
    break;

  case 155: // implements_item_rest: KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block
#line 856 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                               {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::FunctionDeclaration>(yystack_[8].value.as < std::string > (), std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        static_cast<fin::FunctionDeclaration*>(yylhs.value.as < std::unique_ptr<fin::ASTNode> > ().get())->generic_params = std::move(yystack_[7].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2675 "parser.tab.c"
    break;

  case 156: // implements_item_rest: KW_STATIC KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block
#line 862 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                           {
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::make_unique<fin::FunctionDeclaration>(yystack_[8].value.as < std::string > (), std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        auto* func = static_cast<fin::FunctionDeclaration*>(yylhs.value.as < std::unique_ptr<fin::ASTNode> > ().get());
        func->generic_params = std::move(yystack_[7].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        func->is_static = true;
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2687 "parser.tab.c"
    break;

  case 157: // implements_item_rest: KW_OPERATOR operator_symbol operator_generics_opt operator_params_opt LT type GT block
#line 870 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                             {
        auto op = std::make_unique<fin::OperatorDeclaration>(yystack_[6].value.as < fin::ASTTokenKind > (), std::move(yystack_[4].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()), false);
        op->generic_params = std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::GenericParam>> > ());
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > () = std::move(op);
        yylhs.value.as < std::unique_ptr<fin::ASTNode> > ()->setLoc(yylhs.location);
    }
#line 2698 "parser.tab.c"
    break;

  case 158: // special_declaration: AT KW_SPECIAL IDENTIFIER LPAREN params RPAREN LT type GT block
#line 882 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                   {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::SpecialDeclaration>(yystack_[7].value.as < std::string > (), std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 2707 "parser.tab.c"
    break;

  case 159: // special_declaration: attribute_list AT KW_SPECIAL IDENTIFIER LPAREN params RPAREN LT type GT block
#line 887 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                    {
        auto sd = std::make_unique<fin::SpecialDeclaration>(yystack_[7].value.as < std::string > (), std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        sd->attributes = std::move(yystack_[10].value.as < std::vector<std::unique_ptr<fin::Attribute>> > ());
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::move(sd);
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 2718 "parser.tab.c"
    break;

  case 160: // variable_declaration: KW_LET IDENTIFIER LT type GT EQUAL expression SEMICOLON
#line 898 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                            {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::VariableDeclaration>(true, yystack_[6].value.as < std::string > (), std::move(yystack_[4].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 2727 "parser.tab.c"
    break;

  case 161: // variable_declaration: KW_CONST IDENTIFIER LT type GT EQUAL expression SEMICOLON
#line 902 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::VariableDeclaration>(false, yystack_[6].value.as < std::string > (), std::move(yystack_[4].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 2736 "parser.tab.c"
    break;

  case 162: // variable_declaration: KW_LET IDENTIFIER LT type GT SEMICOLON
#line 906 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                             {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::VariableDeclaration>(true, yystack_[4].value.as < std::string > (), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), nullptr);
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 2745 "parser.tab.c"
    break;

  case 163: // type: type_no_annot
#line 917 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                  { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()); }
#line 2751 "parser.tab.c"
    break;

  case 164: // type: base_type LBRACE expression_list RBRACE
#line 918 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                           {
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::move(yystack_[3].value.as < std::unique_ptr<fin::TypeNode> > ());
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->annotations = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ());
    }
#line 2760 "parser.tab.c"
    break;

  case 165: // type: ELLIPSIS
#line 922 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>("..."); }
#line 2766 "parser.tab.c"
    break;

  case 166: // type_no_annot: base_type
#line 926 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                    { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()); }
#line 2772 "parser.tab.c"
    break;

  case 167: // type_no_annot: pointer_type
#line 927 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                   { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()); }
#line 2778 "parser.tab.c"
    break;

  case 168: // type_no_annot: array_type
#line 928 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                 { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()); }
#line 2784 "parser.tab.c"
    break;

  case 169: // base_type: primitive_type
#line 932 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                   { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>(yystack_[0].value.as < std::string > ()); }
#line 2790 "parser.tab.c"
    break;

  case 170: // base_type: IDENTIFIER
#line 933 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                 { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>(yystack_[0].value.as < std::string > ()); }
#line 2796 "parser.tab.c"
    break;

  case 171: // base_type: IDENTIFIER LT type_list GT
#line 934 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                 { 
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>(yystack_[3].value.as < std::string > ()); 
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->generics = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
    }
#line 2805 "parser.tab.c"
    break;

  case 172: // base_type: IDENTIFIER LT type_list SHIFTRIGHT
#line 938 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                         {
        /* Handle >> as two GTs for nested generics */
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>(yystack_[3].value.as < std::string > ());
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->generics = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
    }
#line 2815 "parser.tab.c"
    break;

  case 173: // base_type: LBRACE type_list RBRACE
#line 943 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                              { 
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>("prototype"); 
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->is_prototype = true;
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->generics = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
    }
#line 2825 "parser.tab.c"
    break;

  case 174: // base_type: KW_AUTO
#line 948 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
              { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>("auto"); }
#line 2831 "parser.tab.c"
    break;

  case 175: // base_type: KW_SELF_TYPE
#line 949 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                   { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>("Self"); }
#line 2837 "parser.tab.c"
    break;

  case 176: // base_type: KW_ANY
#line 950 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>("any"); }
#line 2843 "parser.tab.c"
    break;

  case 177: // base_type: KW_ANY LT type_list GT
#line 951 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                             {
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>("any");
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->generics = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
    }
#line 2852 "parser.tab.c"
    break;

  case 178: // base_type: KW_ANY LT type_list SHIFTRIGHT
#line 955 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                     {
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>("any");
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->generics = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
    }
#line 2861 "parser.tab.c"
    break;

  case 179: // base_type: fn_type
#line 959 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
              { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()); }
#line 2867 "parser.tab.c"
    break;

  case 180: // base_type: LPAREN type RPAREN
#line 960 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                         { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::move(yystack_[1].value.as < std::unique_ptr<fin::TypeNode> > ()); }
#line 2873 "parser.tab.c"
    break;

  case 181: // base_type: DOLLAR KW_TYPE
#line 961 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                     { yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::TypeNode>("$type"); yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->setLoc(yylhs.location); }
#line 2879 "parser.tab.c"
    break;

  case 182: // fn_type: KW_FN_TYPE LPAREN type_list RPAREN ARROW type
#line 965 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                  {
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::FunctionTypeNode>(std::move(yystack_[3].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()));
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->setLoc(yylhs.location);
    }
#line 2888 "parser.tab.c"
    break;

  case 183: // fn_type: KW_FN_TYPE LPAREN type_list RPAREN RARROW type
#line 969 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                     {
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::FunctionTypeNode>(std::move(yystack_[3].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()));
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->setLoc(yylhs.location);
    }
#line 2897 "parser.tab.c"
    break;

  case 184: // fn_type: KW_FN_TYPE LPAREN RPAREN ARROW type
#line 974 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                          {
        std::vector<std::unique_ptr<fin::TypeNode>> empty;
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::FunctionTypeNode>(std::move(empty), std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()));
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->setLoc(yylhs.location);
    }
#line 2907 "parser.tab.c"
    break;

  case 185: // type_list: type_list COMMA type
#line 982 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                         { yystack_[2].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ().push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ())); yylhs.value.as < std::vector<std::unique_ptr<fin::TypeNode>> > () = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ()); }
#line 2913 "parser.tab.c"
    break;

  case 186: // type_list: type
#line 983 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
           { std::vector<std::unique_ptr<fin::TypeNode>> v; v.push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ())); yylhs.value.as < std::vector<std::unique_ptr<fin::TypeNode>> > () = std::move(v); }
#line 2919 "parser.tab.c"
    break;

  case 187: // pointer_type: AMPERSAND type
#line 987 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                   {
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::PointerTypeNode>(std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()));
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->setLoc(yylhs.location);
    }
#line 2928 "parser.tab.c"
    break;

  case 188: // pointer_type: MULT type
#line 991 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                {
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::PointerTypeNode>(std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()));
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->setLoc(yylhs.location);
    }
#line 2937 "parser.tab.c"
    break;

  case 189: // pointer_type: AND type
#line 995 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               {
        /* Handle && as a double pointer/reference */
        auto inner = std::make_unique<fin::PointerTypeNode>(std::move(yystack_[0].value.as < std::unique_ptr<fin::TypeNode> > ()));
        inner->setLoc(yylhs.location);
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::PointerTypeNode>(std::move(inner));
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->setLoc(yylhs.location);
    }
#line 2949 "parser.tab.c"
    break;

  case 190: // array_type: LBRACKET type RBRACKET
#line 1005 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                           {
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::ArrayTypeNode>(std::move(yystack_[1].value.as < std::unique_ptr<fin::TypeNode> > ()), nullptr);
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->setLoc(yylhs.location);
    }
#line 2958 "parser.tab.c"
    break;

  case 191: // array_type: LBRACKET type COMMA expression RBRACKET
#line 1009 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                              {
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > () = std::make_unique<fin::ArrayTypeNode>(std::move(yystack_[3].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::unique_ptr<fin::TypeNode> > ()->setLoc(yylhs.location);
    }
#line 2967 "parser.tab.c"
    break;

  case 192: // primitive_type: TYPE_INT
#line 1016 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                  { yylhs.value.as < std::string > () = "int"; }
#line 2973 "parser.tab.c"
    break;

  case 193: // primitive_type: TYPE_LONG
#line 1017 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                  { yylhs.value.as < std::string > () = "long"; }
#line 2979 "parser.tab.c"
    break;

  case 194: // primitive_type: TYPE_FLOAT
#line 1018 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                  { yylhs.value.as < std::string > () = "float"; }
#line 2985 "parser.tab.c"
    break;

  case 195: // primitive_type: TYPE_DOUBLE
#line 1019 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                  { yylhs.value.as < std::string > () = "double"; }
#line 2991 "parser.tab.c"
    break;

  case 196: // primitive_type: TYPE_STRING
#line 1020 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                  { yylhs.value.as < std::string > () = "string"; }
#line 2997 "parser.tab.c"
    break;

  case 197: // primitive_type: TYPE_CHAR
#line 1021 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                  { yylhs.value.as < std::string > () = "char"; }
#line 3003 "parser.tab.c"
    break;

  case 198: // primitive_type: TYPE_VOID
#line 1022 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                  { yylhs.value.as < std::string > () = "void"; }
#line 3009 "parser.tab.c"
    break;

  case 199: // primitive_type: TYPE_BOOL
#line 1023 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                  { yylhs.value.as < std::string > () = "bool"; }
#line 3015 "parser.tab.c"
    break;

  case 200: // if_statement: KW_IF LPAREN expression RPAREN block
#line 1029 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                      {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::IfStatement>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()), nullptr);
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 3024 "parser.tab.c"
    break;

  case 201: // if_statement: KW_IF LPAREN expression RPAREN block KW_ELSE block
#line 1033 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                         {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::IfStatement>(std::move(yystack_[4].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::Block> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 3033 "parser.tab.c"
    break;

  case 202: // while_loop: KW_WHILE LPAREN expression RPAREN block
#line 1040 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                            {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::WhileLoop>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 3042 "parser.tab.c"
    break;

  case 203: // for_loop: KW_FOR LPAREN variable_declaration expression SEMICOLON expression RPAREN block
#line 1047 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                    {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::ForLoop>(std::move(yystack_[5].value.as < std::unique_ptr<fin::Statement> > ()), std::move(yystack_[4].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 3051 "parser.tab.c"
    break;

  case 204: // foreach_loop: KW_FOREACH IDENTIFIER LT type GT KW_IN no_struct_expression block
#line 1055 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                      {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::ForeachLoop>(yystack_[6].value.as < std::string > (), std::move(yystack_[4].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 3060 "parser.tab.c"
    break;

  case 205: // control_statement: KW_BREAK SEMICOLON
#line 1062 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                       { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::BreakStatement>(); yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location); }
#line 3066 "parser.tab.c"
    break;

  case 206: // control_statement: KW_CONTINUE SEMICOLON
#line 1063 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                            { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::ContinueStatement>(); yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location); }
#line 3072 "parser.tab.c"
    break;

  case 207: // delete_statement: KW_DELETE expression SEMICOLON
#line 1067 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                   {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::DeleteStatement>(std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 3081 "parser.tab.c"
    break;

  case 208: // try_catch_statement: KW_TRY block KW_CATCH LPAREN IDENTIFIER KW_AS type RPAREN block
#line 1074 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                    {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::TryCatch>(std::move(yystack_[7].value.as < std::unique_ptr<fin::Block> > ()), yystack_[4].value.as < std::string > (), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 3090 "parser.tab.c"
    break;

  case 209: // blame_statement: KW_BLAME expression SEMICOLON
#line 1081 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                  {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::BlameStatement>(std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 3099 "parser.tab.c"
    break;

  case 210: // blame_statement: KW_BLAME expression COMMA expression SEMICOLON
#line 1085 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                     {
        yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::BlameStatement>(std::move(yystack_[3].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::unique_ptr<fin::Statement> > ()->setLoc(yylhs.location);
    }
#line 3108 "parser.tab.c"
    break;

  case 211: // return_statement: KW_RETURN expression SEMICOLON
#line 1092 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                   { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::ReturnStatement>(std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ())); }
#line 3114 "parser.tab.c"
    break;

  case 212: // return_statement: KW_RETURN SEMICOLON
#line 1093 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                          { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::ReturnStatement>(nullptr); }
#line 3120 "parser.tab.c"
    break;

  case 213: // expression_statement: expression SEMICOLON
#line 1099 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                         { yylhs.value.as < std::unique_ptr<fin::Statement> > () = std::make_unique<fin::ExpressionStatement>(std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ())); }
#line 3126 "parser.tab.c"
    break;

  case 214: // lambda_expression: KW_FUN LPAREN params RPAREN LT type GT block
#line 1104 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                   {
          yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::LambdaExpression>(std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
          yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
      }
#line 3135 "parser.tab.c"
    break;

  case 215: // lambda_expression: LPAREN params RPAREN LT type GT ARROW block
#line 1109 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                    {
          yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::LambdaExpression>(std::move(yystack_[6].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[3].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
          yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
      }
#line 3144 "parser.tab.c"
    break;

  case 216: // lambda_expression: LPAREN params RPAREN LT type GT ARROW expression
#line 1114 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                     {
          yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::LambdaExpression>(std::move(yystack_[6].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[3].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()));
          yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
      }
#line 3153 "parser.tab.c"
    break;

  case 217: // expression: expression EQUAL expression
#line 1124 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::EQUAL, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3159 "parser.tab.c"
    break;

  case 218: // expression: expression PLUSEQUAL expression
#line 1125 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                      { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::PLUSEQUAL, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3165 "parser.tab.c"
    break;

  case 219: // expression: expression MINUSEQUAL expression
#line 1126 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                       { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::MINUSEQUAL, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3171 "parser.tab.c"
    break;

  case 220: // expression: expression MULTEQUAL expression
#line 1127 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                      { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::MULTEQUAL, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3177 "parser.tab.c"
    break;

  case 221: // expression: expression DIVEQUAL expression
#line 1128 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::DIVEQUAL, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3183 "parser.tab.c"
    break;

  case 222: // expression: expression OR expression
#line 1129 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                               { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::OR, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3189 "parser.tab.c"
    break;

  case 223: // expression: expression AND expression
#line 1130 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::AND, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3195 "parser.tab.c"
    break;

  case 224: // expression: expression EQEQ expression
#line 1131 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                 { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::EQEQ, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3201 "parser.tab.c"
    break;

  case 225: // expression: expression NOTEQ expression
#line 1132 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                  { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::NOTEQ, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3207 "parser.tab.c"
    break;

  case 226: // expression: expression LT expression
#line 1133 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                               { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::LT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3213 "parser.tab.c"
    break;

  case 227: // expression: expression GT expression
#line 1134 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                               { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::GT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3219 "parser.tab.c"
    break;

  case 228: // expression: expression LTEQ expression
#line 1135 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                 { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::LTEQ, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3225 "parser.tab.c"
    break;

  case 229: // expression: expression GTEQ expression
#line 1136 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                 { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::GTEQ, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3231 "parser.tab.c"
    break;

  case 230: // expression: expression PLUS expression
#line 1137 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                 { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::PLUS, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3237 "parser.tab.c"
    break;

  case 231: // expression: expression MINUS expression
#line 1138 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                  { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::MINUS, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3243 "parser.tab.c"
    break;

  case 232: // expression: expression MULT expression
#line 1139 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                 { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::MULT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3249 "parser.tab.c"
    break;

  case 233: // expression: expression DIV expression
#line 1140 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::DIV, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3255 "parser.tab.c"
    break;

  case 234: // expression: expression MOD expression
#line 1141 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::MOD, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3261 "parser.tab.c"
    break;

  case 235: // expression: expression SHIFTLEFT expression
#line 1142 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                      { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::SHIFTLEFT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3267 "parser.tab.c"
    break;

  case 236: // expression: expression SHIFTRIGHT expression
#line 1143 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                       { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::SHIFTRIGHT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3273 "parser.tab.c"
    break;

  case 237: // expression: MINUS expression
#line 1146 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                    { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MINUS, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3279 "parser.tab.c"
    break;

  case 238: // expression: NOT expression
#line 1147 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::NOT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3285 "parser.tab.c"
    break;

  case 239: // expression: AMPERSAND expression
#line 1148 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::AMPERSAND, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3291 "parser.tab.c"
    break;

  case 240: // expression: MULT expression
#line 1149 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                             { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MULT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3297 "parser.tab.c"
    break;

  case 241: // expression: INCREMENT expression
#line 1150 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                           { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3303 "parser.tab.c"
    break;

  case 242: // expression: DECREMENT expression
#line 1151 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                           { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3309 "parser.tab.c"
    break;

  case 243: // expression: expression LPAREN arguments RPAREN
#line 1154 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                         {
        if (auto* id = dynamic_cast<fin::Identifier*>(yystack_[3].value.as < std::unique_ptr<fin::Expression> > ().get())) {
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::FunctionCall>(id->name, std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        } else if (auto* mem = dynamic_cast<fin::MemberAccess*>(yystack_[3].value.as < std::unique_ptr<fin::Expression> > ().get())) {
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::MethodCall>(std::move(mem->object), mem->member, std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        } else {
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::FunctionCall>("unknown", std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        }
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3324 "parser.tab.c"
    break;

  case 244: // expression: expression DOT IDENTIFIER
#line 1164 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::MemberAccess>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), yystack_[0].value.as < std::string > ());
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3333 "parser.tab.c"
    break;

  case 245: // expression: expression INCREMENT
#line 1168 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                           { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3339 "parser.tab.c"
    break;

  case 246: // expression: expression DECREMENT
#line 1169 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                           { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3345 "parser.tab.c"
    break;

  case 247: // expression: expression LBRACKET expression RBRACKET
#line 1170 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                              {
           yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::ArrayAccess>(std::move(yystack_[3].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()));
           yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3354 "parser.tab.c"
    break;

  case 248: // expression: expression QUESTION expression COLON expression
#line 1174 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                      {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::TernaryOp>(std::move(yystack_[4].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3363 "parser.tab.c"
    break;

  case 249: // expression: expression NOT LPAREN arguments RPAREN
#line 1179 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                             {
        std::string name = flatten_macro_name(yystack_[4].value.as < std::unique_ptr<fin::Expression> > ().get());
        if (name.empty()) {
            error(yystack_[4].location, "Invalid macro name (must be identifier or dotted path)");
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = nullptr; // Error recovery
        } else {
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::MacroInvocation>(name, std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
            yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
        }
    }
#line 3378 "parser.tab.c"
    break;

  case 250: // expression: primary_no_struct
#line 1191 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                        { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()); }
#line 3384 "parser.tab.c"
    break;

  case 251: // expression: lambda_expression
#line 1193 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                        { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()); }
#line 3390 "parser.tab.c"
    break;

  case 252: // expression: IDENTIFIER LBRACE field_assignments RBRACE
#line 1196 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                 {
        std::vector<std::unique_ptr<fin::TypeNode>> empty_generics;
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::StructInstantiation>(yystack_[3].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()), std::move(empty_generics));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3400 "parser.tab.c"
    break;

  case 253: // expression: IDENTIFIER DOUBLE_COLON LT type_list GT LBRACE field_assignments RBRACE
#line 1202 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                              {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::StructInstantiation>(yystack_[7].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()), std::move(yystack_[4].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3409 "parser.tab.c"
    break;

  case 254: // expression: KW_NEW IDENTIFIER LBRACE field_assignments RBRACE
#line 1207 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                        {
        auto type = std::make_unique<fin::TypeNode>(yystack_[3].value.as < std::string > ());
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::NewExpression>(std::move(type), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3419 "parser.tab.c"
    break;

  case 255: // expression: KW_NEW IDENTIFIER DOUBLE_COLON LT type_list GT LBRACE field_assignments RBRACE
#line 1213 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                     {
        auto type = std::make_unique<fin::TypeNode>(yystack_[7].value.as < std::string > ());
        type->generics = std::move(yystack_[4].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::NewExpression>(std::move(type), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3430 "parser.tab.c"
    break;

  case 256: // expression: KW_NEW AMPERSAND type LBRACE field_assignments RBRACE
#line 1220 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                            {
        auto ptr_type = std::make_unique<fin::PointerTypeNode>(std::move(yystack_[3].value.as < std::unique_ptr<fin::TypeNode> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::NewExpression>(std::move(ptr_type), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3440 "parser.tab.c"
    break;

  case 257: // expression: KW_NEW LBRACKET type RBRACKET LBRACE field_assignments RBRACE
#line 1226 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                    {
        auto arr_type = std::make_unique<fin::ArrayTypeNode>(std::move(yystack_[4].value.as < std::unique_ptr<fin::TypeNode> > ()), nullptr);
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::NewExpression>(std::move(arr_type), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3450 "parser.tab.c"
    break;

  case 258: // expression: KW_NEW LBRACKET type COMMA expression RBRACKET LBRACE field_assignments RBRACE
#line 1231 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                     {
        auto arr_type = std::make_unique<fin::ArrayTypeNode>(std::move(yystack_[6].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[4].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::NewExpression>(std::move(arr_type), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3460 "parser.tab.c"
    break;

  case 259: // expression: KW_NEW KW_SELF_TYPE LBRACE field_assignments RBRACE
#line 1237 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                          {
        auto type = std::make_unique<fin::TypeNode>("Self");
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::NewExpression>(std::move(type), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3470 "parser.tab.c"
    break;

  case 260: // no_struct_expression: no_struct_expression EQUAL no_struct_expression
#line 1250 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                    { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::EQUAL, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3476 "parser.tab.c"
    break;

  case 261: // no_struct_expression: no_struct_expression PLUSEQUAL no_struct_expression
#line 1251 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                          { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::PLUSEQUAL, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3482 "parser.tab.c"
    break;

  case 262: // no_struct_expression: no_struct_expression MINUSEQUAL no_struct_expression
#line 1252 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                           { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::MINUSEQUAL, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3488 "parser.tab.c"
    break;

  case 263: // no_struct_expression: no_struct_expression MULTEQUAL no_struct_expression
#line 1253 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                          { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::MULTEQUAL, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3494 "parser.tab.c"
    break;

  case 264: // no_struct_expression: no_struct_expression DIVEQUAL no_struct_expression
#line 1254 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                         { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::DIVEQUAL, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3500 "parser.tab.c"
    break;

  case 265: // no_struct_expression: no_struct_expression PIPE PIPE no_struct_expression
#line 1255 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                          { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[3].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::OR, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3506 "parser.tab.c"
    break;

  case 266: // no_struct_expression: no_struct_expression AMPERSAND AMPERSAND no_struct_expression
#line 1256 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                    { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[3].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::AND, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3512 "parser.tab.c"
    break;

  case 267: // no_struct_expression: no_struct_expression EQEQ no_struct_expression
#line 1257 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::EQEQ, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3518 "parser.tab.c"
    break;

  case 268: // no_struct_expression: no_struct_expression NOTEQ no_struct_expression
#line 1258 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                      { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::NOTEQ, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3524 "parser.tab.c"
    break;

  case 269: // no_struct_expression: no_struct_expression LT no_struct_expression
#line 1259 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                   { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::LT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3530 "parser.tab.c"
    break;

  case 270: // no_struct_expression: no_struct_expression GT no_struct_expression
#line 1260 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                   { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::GT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3536 "parser.tab.c"
    break;

  case 271: // no_struct_expression: no_struct_expression LTEQ no_struct_expression
#line 1261 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::LTEQ, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3542 "parser.tab.c"
    break;

  case 272: // no_struct_expression: no_struct_expression GTEQ no_struct_expression
#line 1262 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::GTEQ, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3548 "parser.tab.c"
    break;

  case 273: // no_struct_expression: no_struct_expression PLUS no_struct_expression
#line 1263 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::PLUS, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3554 "parser.tab.c"
    break;

  case 274: // no_struct_expression: no_struct_expression MINUS no_struct_expression
#line 1264 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                      { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::MINUS, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3560 "parser.tab.c"
    break;

  case 275: // no_struct_expression: no_struct_expression MULT no_struct_expression
#line 1265 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::MULT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3566 "parser.tab.c"
    break;

  case 276: // no_struct_expression: no_struct_expression DIV no_struct_expression
#line 1266 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                    { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::DIV, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3572 "parser.tab.c"
    break;

  case 277: // no_struct_expression: no_struct_expression MOD no_struct_expression
#line 1267 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                    { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::MOD, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3578 "parser.tab.c"
    break;

  case 278: // no_struct_expression: no_struct_expression LT LT no_struct_expression
#line 1268 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                      { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[3].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::SHIFTLEFT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3584 "parser.tab.c"
    break;

  case 279: // no_struct_expression: no_struct_expression GT GT no_struct_expression
#line 1269 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                      { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::BinaryOp>(std::move(yystack_[3].value.as < std::unique_ptr<fin::Expression> > ()), fin::ASTTokenKind::SHIFTRIGHT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3590 "parser.tab.c"
    break;

  case 280: // no_struct_expression: MINUS no_struct_expression
#line 1272 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                              { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MINUS, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3596 "parser.tab.c"
    break;

  case 281: // no_struct_expression: NOT no_struct_expression
#line 1273 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                               { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::NOT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3602 "parser.tab.c"
    break;

  case 282: // no_struct_expression: AMPERSAND no_struct_expression
#line 1274 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                          { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::AMPERSAND, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3608 "parser.tab.c"
    break;

  case 283: // no_struct_expression: MULT no_struct_expression
#line 1275 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                       { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MULT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3614 "parser.tab.c"
    break;

  case 284: // no_struct_expression: INCREMENT no_struct_expression
#line 1276 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3620 "parser.tab.c"
    break;

  case 285: // no_struct_expression: DECREMENT no_struct_expression
#line 1277 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3626 "parser.tab.c"
    break;

  case 286: // no_struct_expression: no_struct_expression LPAREN arguments RPAREN
#line 1280 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                   {
        if (auto* id = dynamic_cast<fin::Identifier*>(yystack_[3].value.as < std::unique_ptr<fin::Expression> > ().get())) {
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::FunctionCall>(id->name, std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        } else if (auto* mem = dynamic_cast<fin::MemberAccess*>(yystack_[3].value.as < std::unique_ptr<fin::Expression> > ().get())) {
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::MethodCall>(std::move(mem->object), mem->member, std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        } else {
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::FunctionCall>("unknown", std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        }
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3641 "parser.tab.c"
    break;

  case 287: // no_struct_expression: no_struct_expression DOT IDENTIFIER
#line 1290 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                          {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::MemberAccess>(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), yystack_[0].value.as < std::string > ());
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3650 "parser.tab.c"
    break;

  case 288: // no_struct_expression: no_struct_expression INCREMENT
#line 1294 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3656 "parser.tab.c"
    break;

  case 289: // no_struct_expression: no_struct_expression DECREMENT
#line 1295 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3662 "parser.tab.c"
    break;

  case 290: // no_struct_expression: no_struct_expression LBRACKET expression RBRACKET
#line 1296 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                        {
           yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::ArrayAccess>(std::move(yystack_[3].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()));
           yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3671 "parser.tab.c"
    break;

  case 291: // no_struct_expression: no_struct_expression QUESTION no_struct_expression COLON no_struct_expression
#line 1300 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                    {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::TernaryOp>(std::move(yystack_[4].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3680 "parser.tab.c"
    break;

  case 292: // no_struct_expression: no_struct_expression NOT LPAREN macro_arguments RPAREN
#line 1305 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                             {
        std::string name = flatten_macro_name(yystack_[4].value.as < std::unique_ptr<fin::Expression> > ().get());
        if (name.empty()) {
            error(yystack_[4].location, "Invalid macro name");
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = nullptr;
        } else {
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::MacroInvocation>(name, std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
            yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
        }
    }
#line 3695 "parser.tab.c"
    break;

  case 293: // no_struct_expression: no_struct_expression NOT LBRACE macro_arguments RBRACE
#line 1315 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                             {
        std::string name = flatten_macro_name(yystack_[4].value.as < std::unique_ptr<fin::Expression> > ().get());
        if (name.empty()) {
            error(yystack_[4].location, "Invalid macro name");
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = nullptr;
        } else {
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::MacroInvocation>(name, std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
            yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
        }
    }
#line 3710 "parser.tab.c"
    break;

  case 294: // no_struct_expression: no_struct_expression NOT LBRACKET macro_arguments RBRACKET
#line 1325 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                 {
        std::string name = flatten_macro_name(yystack_[4].value.as < std::unique_ptr<fin::Expression> > ().get());
        if (name.empty()) {
            error(yystack_[4].location, "Invalid macro name");
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = nullptr;
        } else {
            yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::MacroInvocation>(name, std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
            yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
        }
    }
#line 3725 "parser.tab.c"
    break;

  case 295: // no_struct_expression: primary_no_struct
#line 1337 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                        { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()); }
#line 3731 "parser.tab.c"
    break;

  case 296: // no_struct_expression: KW_FUN LPAREN params RPAREN LT type GT block
#line 1340 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                   {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::LambdaExpression>(std::move(yystack_[5].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[2].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3740 "parser.tab.c"
    break;

  case 297: // no_struct_expression: LPAREN params RPAREN LT type GT ARROW block
#line 1344 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                  {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::LambdaExpression>(std::move(yystack_[6].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[3].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3749 "parser.tab.c"
    break;

  case 298: // no_struct_expression: LPAREN params RPAREN LT type GT ARROW no_struct_expression
#line 1348 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                 {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::LambdaExpression>(std::move(yystack_[6].value.as < std::vector<std::unique_ptr<fin::Parameter>> > ()), std::move(yystack_[3].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3758 "parser.tab.c"
    break;

  case 299: // no_struct_expression: KW_NEW IDENTIFIER LBRACE field_assignments RBRACE
#line 1354 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                        {
        auto type = std::make_unique<fin::TypeNode>(yystack_[3].value.as < std::string > ());
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::NewExpression>(std::move(type), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3768 "parser.tab.c"
    break;

  case 300: // no_struct_expression: KW_NEW IDENTIFIER DOUBLE_COLON LT type_list GT LBRACE field_assignments RBRACE
#line 1360 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                     {
        auto type = std::make_unique<fin::TypeNode>(yystack_[7].value.as < std::string > ());
        type->generics = std::move(yystack_[4].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::NewExpression>(std::move(type), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3779 "parser.tab.c"
    break;

  case 301: // no_struct_expression: KW_NEW AMPERSAND type LBRACE field_assignments RBRACE
#line 1367 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                            {
        auto ptr_type = std::make_unique<fin::PointerTypeNode>(std::move(yystack_[3].value.as < std::unique_ptr<fin::TypeNode> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::NewExpression>(std::move(ptr_type), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3789 "parser.tab.c"
    break;

  case 302: // no_struct_expression: KW_NEW KW_SELF_TYPE LBRACE field_assignments RBRACE
#line 1373 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                          {
        auto type = std::make_unique<fin::TypeNode>("Self");
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::NewExpression>(std::move(type), std::move(yystack_[1].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3799 "parser.tab.c"
    break;

  case 303: // static_method_call: IDENTIFIER DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN
#line 1382 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                               {
        auto type = std::make_unique<fin::TypeNode>(yystack_[5].value.as < std::string > ());
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::StaticMethodCall>(std::move(type), yystack_[3].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3809 "parser.tab.c"
    break;

  case 304: // static_method_call: IDENTIFIER DOUBLE_COLON LT type_list GT DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN
#line 1388 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                                              {
        auto type = std::make_unique<fin::TypeNode>(yystack_[9].value.as < std::string > ());
        type->generics = std::move(yystack_[6].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::StaticMethodCall>(std::move(type), yystack_[3].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3820 "parser.tab.c"
    break;

  case 305: // static_method_call: KW_SELF_TYPE DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN
#line 1395 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                   {
        auto type = std::make_unique<fin::TypeNode>("Self");
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::StaticMethodCall>(std::move(type), yystack_[3].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3830 "parser.tab.c"
    break;

  case 306: // primary_no_struct: IDENTIFIER
#line 1403 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::Identifier>(yystack_[0].value.as < std::string > ()); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3836 "parser.tab.c"
    break;

  case 307: // primary_no_struct: literal
#line 1404 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
              { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()); }
#line 3842 "parser.tab.c"
    break;

  case 308: // primary_no_struct: LPAREN expression RPAREN
#line 1405 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                               { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ()); }
#line 3848 "parser.tab.c"
    break;

  case 309: // primary_no_struct: prototype_literal
#line 1406 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                        { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()); }
#line 3854 "parser.tab.c"
    break;

  case 310: // primary_no_struct: DOLLAR IDENTIFIER
#line 1409 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                        { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::Identifier>("$" + yystack_[0].value.as < std::string > ()); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3860 "parser.tab.c"
    break;

  case 311: // primary_no_struct: LBRACKET arguments RBRACKET
#line 1411 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                  { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::ArrayLiteral>(std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3866 "parser.tab.c"
    break;

  case 312: // primary_no_struct: KW_CAST LT type GT LPAREN expression RPAREN
#line 1412 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                  { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::CastExpression>(std::move(yystack_[4].value.as < std::unique_ptr<fin::TypeNode> > ()), std::move(yystack_[1].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3872 "parser.tab.c"
    break;

  case 313: // primary_no_struct: KW_SIZEOF LPAREN type RPAREN
#line 1413 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                   { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::SizeofExpression>(std::move(yystack_[1].value.as < std::unique_ptr<fin::TypeNode> > ())); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3878 "parser.tab.c"
    break;

  case 314: // primary_no_struct: IDENTIFIER DOUBLE_COLON LT type_list GT LPAREN arguments RPAREN
#line 1416 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                                      {
        auto call = std::make_unique<fin::FunctionCall>(yystack_[7].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        call->generic_args = std::move(yystack_[4].value.as < std::vector<std::unique_ptr<fin::TypeNode>> > ());
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::move(call);
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3889 "parser.tab.c"
    break;

  case 315: // primary_no_struct: KW_SELF_TYPE LPAREN arguments RPAREN
#line 1424 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                           {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::FunctionCall>("Self", std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3898 "parser.tab.c"
    break;

  case 316: // primary_no_struct: static_method_call
#line 1432 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                         { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()); }
#line 3904 "parser.tab.c"
    break;

  case 317: // primary_no_struct: KW_QUOTE block
#line 1435 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                     {
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::QuoteExpression>(std::move(yystack_[0].value.as < std::unique_ptr<fin::Block> > ()));
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location);
    }
#line 3913 "parser.tab.c"
    break;

  case 318: // primary_no_struct: super_expression
#line 1441 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                       { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()); }
#line 3919 "parser.tab.c"
    break;

  case 319: // literal: INTEGER
#line 1445 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
            { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::Literal>(yystack_[0].value.as < std::string > (), fin::ASTTokenKind::INTEGER); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3925 "parser.tab.c"
    break;

  case 320: // literal: FLOAT
#line 1446 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
            { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::Literal>(yystack_[0].value.as < std::string > (), fin::ASTTokenKind::FLOAT); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3931 "parser.tab.c"
    break;

  case 321: // literal: STRING_LITERAL
#line 1447 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                     { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::Literal>(yystack_[0].value.as < std::string > (), fin::ASTTokenKind::STRING_LITERAL); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3937 "parser.tab.c"
    break;

  case 322: // literal: CHAR_LITERAL
#line 1448 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                   { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::Literal>(yystack_[0].value.as < std::string > (), fin::ASTTokenKind::CHAR_LITERAL); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3943 "parser.tab.c"
    break;

  case 323: // literal: KW_NULL
#line 1449 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
              { yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::Literal>("null", fin::ASTTokenKind::KW_NULL); yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); }
#line 3949 "parser.tab.c"
    break;

  case 324: // arguments: expression_list
#line 1453 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                    { yylhs.value.as < std::vector<std::unique_ptr<fin::Expression>> > () = std::move(yystack_[0].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()); }
#line 3955 "parser.tab.c"
    break;

  case 325: // arguments: %empty
#line 1454 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::vector<std::unique_ptr<fin::Expression>> > () = std::vector<std::unique_ptr<fin::Expression>>(); }
#line 3961 "parser.tab.c"
    break;

  case 326: // expression_list: expression_list COMMA expression
#line 1458 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                     { yystack_[2].value.as < std::vector<std::unique_ptr<fin::Expression>> > ().push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::vector<std::unique_ptr<fin::Expression>> > () = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()); }
#line 3967 "parser.tab.c"
    break;

  case 327: // expression_list: expression
#line 1459 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                 { std::vector<std::unique_ptr<fin::Expression>> v; v.push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); yylhs.value.as < std::vector<std::unique_ptr<fin::Expression>> > () = std::move(v); }
#line 3973 "parser.tab.c"
    break;

  case 328: // macro_arg_item: expression
#line 1463 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               {
        std::vector<std::unique_ptr<fin::Expression>> v;
        v.push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::vector<std::unique_ptr<fin::Expression>> > () = std::move(v);
    }
#line 3983 "parser.tab.c"
    break;

  case 329: // macro_arg_item: expression COLON expression
#line 1468 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                  {
        std::vector<std::unique_ptr<fin::Expression>> v;
        v.push_back(std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()));
        v.push_back(std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ()));
        yylhs.value.as < std::vector<std::unique_ptr<fin::Expression>> > () = std::move(v);
    }
#line 3994 "parser.tab.c"
    break;

  case 330: // macro_arg_list_body: macro_arg_list_body COMMA macro_arg_item
#line 1477 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                             {
        for(auto& e : yystack_[0].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()) yystack_[2].value.as < std::vector<std::unique_ptr<fin::Expression>> > ().push_back(std::move(e));
        yylhs.value.as < std::vector<std::unique_ptr<fin::Expression>> > () = std::move(yystack_[2].value.as < std::vector<std::unique_ptr<fin::Expression>> > ());
    }
#line 4003 "parser.tab.c"
    break;

  case 331: // macro_arg_list_body: macro_arg_item
#line 1481 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                     { yylhs.value.as < std::vector<std::unique_ptr<fin::Expression>> > () = std::move(yystack_[0].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()); }
#line 4009 "parser.tab.c"
    break;

  case 332: // macro_arguments: macro_arg_list_body
#line 1485 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                        { yylhs.value.as < std::vector<std::unique_ptr<fin::Expression>> > () = std::move(yystack_[0].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()); }
#line 4015 "parser.tab.c"
    break;

  case 333: // macro_arguments: macro_arg_list_body COMMA
#line 1486 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                { yylhs.value.as < std::vector<std::unique_ptr<fin::Expression>> > () = std::move(yystack_[1].value.as < std::vector<std::unique_ptr<fin::Expression>> > ()); }
#line 4021 "parser.tab.c"
    break;

  case 334: // macro_arguments: %empty
#line 1487 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::vector<std::unique_ptr<fin::Expression>> > () = std::vector<std::unique_ptr<fin::Expression>>(); }
#line 4027 "parser.tab.c"
    break;

  case 335: // field_assignments: field_assignments COMMA IDENTIFIER COLON expression
#line 1491 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                        {
        yystack_[4].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ().push_back({yystack_[2].value.as < std::string > (), std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())}); yylhs.value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > () = std::move(yystack_[4].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ());
    }
#line 4035 "parser.tab.c"
    break;

  case 336: // field_assignments: IDENTIFIER COLON expression
#line 1494 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                  {
        std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> v;
        v.push_back({yystack_[2].value.as < std::string > (), std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())}); yylhs.value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > () = std::move(v);
    }
#line 4044 "parser.tab.c"
    break;

  case 337: // field_assignments: %empty
#line 1498 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > () = std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>>(); }
#line 4050 "parser.tab.c"
    break;

  case 338: // visibility_opt: KW_PUB
#line 1504 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
           { yylhs.value.as < bool > () = true; }
#line 4056 "parser.tab.c"
    break;

  case 339: // visibility_opt: KW_PRIV
#line 1505 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
              { yylhs.value.as < bool > () = false; }
#line 4062 "parser.tab.c"
    break;

  case 340: // visibility_opt: %empty
#line 1506 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { yylhs.value.as < bool > () = false; }
#line 4068 "parser.tab.c"
    break;

  case 341: // enum_values: enum_values COMMA enum_value
#line 1510 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                 { yystack_[2].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ().push_back(std::move(yystack_[0].value.as < std::pair<std::string, std::unique_ptr<fin::Expression>> > ())); yylhs.value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > () = std::move(yystack_[2].value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ()); }
#line 4074 "parser.tab.c"
    break;

  case 342: // enum_values: enum_value
#line 1511 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                 { std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> v; v.push_back(std::move(yystack_[0].value.as < std::pair<std::string, std::unique_ptr<fin::Expression>> > ())); yylhs.value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > () = std::move(v); }
#line 4080 "parser.tab.c"
    break;

  case 343: // enum_values: %empty
#line 1512 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
             { std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> v; yylhs.value.as < std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > () = std::move(v); }
#line 4086 "parser.tab.c"
    break;

  case 344: // enum_value: IDENTIFIER
#line 1516 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
               { 
        yylhs.value.as < std::pair<std::string, std::unique_ptr<fin::Expression>> > () = std::make_pair(yystack_[0].value.as < std::string > (), nullptr); 
    }
#line 4094 "parser.tab.c"
    break;

  case 345: // enum_value: IDENTIFIER EQUAL expression
#line 1519 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                  { 
        yylhs.value.as < std::pair<std::string, std::unique_ptr<fin::Expression>> > () = std::make_pair(yystack_[2].value.as < std::string > (), std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())); 
    }
#line 4102 "parser.tab.c"
    break;

  case 346: // prototype_literal: LBRACE prototype_elements RBRACE
#line 1525 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                     { 
        yylhs.value.as < std::unique_ptr<fin::Expression> > () = std::make_unique<fin::PrototypeLiteral>(std::move(yystack_[1].value.as < std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > ())); 
        yylhs.value.as < std::unique_ptr<fin::Expression> > ()->setLoc(yylhs.location); 
    }
#line 4111 "parser.tab.c"
    break;

  case 347: // prototype_elements: prototype_elements COMMA expression COLON expression
#line 1532 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                                           { yystack_[4].value.as < std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > ().push_back({std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())}); yylhs.value.as < std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > () = std::move(yystack_[4].value.as < std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > ()); }
#line 4117 "parser.tab.c"
    break;

  case 348: // prototype_elements: expression COLON expression
#line 1533 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
                                  { std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> v; v.push_back({std::move(yystack_[2].value.as < std::unique_ptr<fin::Expression> > ()), std::move(yystack_[0].value.as < std::unique_ptr<fin::Expression> > ())}); yylhs.value.as < std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > () = std::move(v); }
#line 4123 "parser.tab.c"
    break;


#line 4127 "parser.tab.c"

            default:
              break;
            }
        }
#if YY_EXCEPTIONS
      catch (const syntax_error& yyexc)
        {
          YYCDEBUG << "Caught exception: " << yyexc.what() << '\n';
          error (yyexc);
          YYERROR;
        }
#endif // YY_EXCEPTIONS
      YY_SYMBOL_PRINT ("-> $$ =", yylhs);
      yypop_ (yylen);
      yylen = 0;

      // Shift the result of the reduction.
      yypush_ (YY_NULLPTR, YY_MOVE (yylhs));
    }
    goto yynewstate;


  /*--------------------------------------.
  | yyerrlab -- here on detecting error.  |
  `--------------------------------------*/
  yyerrlab:
    // If not already recovering from an error, report this error.
    if (!yyerrstatus_)
      {
        ++yynerrs_;
        context yyctx (*this, yyla);
        std::string msg = yysyntax_error_ (yyctx);
        error (yyla.location, YY_MOVE (msg));
      }


    yyerror_range[1].location = yyla.location;
    if (yyerrstatus_ == 3)
      {
        /* If just tried and failed to reuse lookahead token after an
           error, discard it.  */

        // Return failure if at end of input.
        if (yyla.kind () == symbol_kind::S_YYEOF)
          YYABORT;
        else if (!yyla.empty ())
          {
            yy_destroy_ ("Error: discarding", yyla);
            yyla.clear ();
          }
      }

    // Else will try to reuse lookahead token after shifting the error token.
    goto yyerrlab1;


  /*---------------------------------------------------.
  | yyerrorlab -- error raised explicitly by YYERROR.  |
  `---------------------------------------------------*/
  yyerrorlab:
    /* Pacify compilers when the user code never invokes YYERROR and
       the label yyerrorlab therefore never appears in user code.  */
    if (false)
      YYERROR;

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYERROR.  */
    yypop_ (yylen);
    yylen = 0;
    YY_STACK_PRINT ();
    goto yyerrlab1;


  /*-------------------------------------------------------------.
  | yyerrlab1 -- common code for both syntax error and YYERROR.  |
  `-------------------------------------------------------------*/
  yyerrlab1:
    yyerrstatus_ = 3;   // Each real token shifted decrements this.
    // Pop stack until we find a state that shifts the error token.
    for (;;)
      {
        yyn = yypact_[+yystack_[0].state];
        if (!yy_pact_value_is_default_ (yyn))
          {
            yyn += symbol_kind::S_YYerror;
            if (0 <= yyn && yyn <= yylast_
                && yycheck_[yyn] == symbol_kind::S_YYerror)
              {
                yyn = yytable_[yyn];
                if (0 < yyn)
                  break;
              }
          }

        // Pop the current state because it cannot handle the error token.
        if (yystack_.size () == 1)
          YYABORT;

        yyerror_range[1].location = yystack_[0].location;
        yy_destroy_ ("Error: popping", yystack_[0]);
        yypop_ ();
        YY_STACK_PRINT ();
      }
    {
      stack_symbol_type error_token;

      yyerror_range[2].location = yyla.location;
      YYLLOC_DEFAULT (error_token.location, yyerror_range, 2);

      // Shift the error token.
      error_token.state = state_type (yyn);
      yypush_ ("Shifting", YY_MOVE (error_token));
    }
    goto yynewstate;


  /*-------------------------------------.
  | yyacceptlab -- YYACCEPT comes here.  |
  `-------------------------------------*/
  yyacceptlab:
    yyresult = 0;
    goto yyreturn;


  /*-----------------------------------.
  | yyabortlab -- YYABORT comes here.  |
  `-----------------------------------*/
  yyabortlab:
    yyresult = 1;
    goto yyreturn;


  /*-----------------------------------------------------.
  | yyreturn -- parsing is finished, return the result.  |
  `-----------------------------------------------------*/
  yyreturn:
    if (!yyla.empty ())
      yy_destroy_ ("Cleanup: discarding lookahead", yyla);

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYABORT or YYACCEPT.  */
    yypop_ (yylen);
    YY_STACK_PRINT ();
    while (1 < yystack_.size ())
      {
        yy_destroy_ ("Cleanup: popping", yystack_[0]);
        yypop_ ();
      }

    return yyresult;
  }
#if YY_EXCEPTIONS
    catch (...)
      {
        YYCDEBUG << "Exception caught: cleaning lookahead and stack\n";
        // Do not try to display the values of the reclaimed symbols,
        // as their printers might throw an exception.
        if (!yyla.empty ())
          yy_destroy_ (YY_NULLPTR, yyla);

        while (1 < yystack_.size ())
          {
            yy_destroy_ (YY_NULLPTR, yystack_[0]);
            yypop_ ();
          }
        throw;
      }
#endif // YY_EXCEPTIONS
  }

  void
  parser::error (const syntax_error& yyexc)
  {
    error (yyexc.location, yyexc.what ());
  }

  const char *
  parser::symbol_name (symbol_kind_type yysymbol)
  {
    static const char *const yy_sname[] =
    {
    "end of file", "error", "invalid token", "IDENTIFIER", "INTEGER",
  "FLOAT", "STRING_LITERAL", "CHAR_LITERAL", "TYPE_ID", "KW_LET",
  "KW_CONST", "KW_AUTO", "KW_FUN", "KW_RETURN", "KW_PUB", "KW_PRIV",
  "KW_STRUCT", "KW_ENUM", "KW_INTERFACE", "KW_MACRO", "KW_STATIC",
  "KW_NULL", "KW_WHILE", "KW_FOR", "KW_FOREACH", "KW_BREAK", "KW_CONTINUE",
  "KW_IF", "KW_ELSE", "KW_ELSEIF", "KW_IN", "KW_TRY", "KW_CATCH",
  "KW_BLAME", "KW_SUPER", "KW_SELF_TYPE", "KW_IMPORT", "KW_AS", "KW_FROM",
  "KW_NEW", "KW_DELETE", "KW_SIZEOF", "KW_TYPEOF", "KW_AS_PTR", "KW_CAST",
  "KW_OPERATOR", "KW_SPECIAL", "KW_FN_TYPE", "KW_DEFINE", "KW_M1778",
  "KW_TYPE", "KW_CLASS", "KW_IMPLEMENTS", "KW_ANY", "TYPE_INT",
  "TYPE_FLOAT", "TYPE_DOUBLE", "TYPE_BOOL", "TYPE_STRING", "TYPE_CHAR",
  "TYPE_VOID", "TYPE_LONG", "LPAREN", "RPAREN", "LBRACE", "RBRACE",
  "LBRACKET", "RBRACKET", "SEMICOLON", "COLON", "DOUBLE_COLON", "COMMA",
  "DOT", "ELLIPSIS", "AT", "DOLLAR", "HASH", "TILDE", "QUESTION", "EQUAL",
  "PLUSEQUAL", "MINUSEQUAL", "MULTEQUAL", "DIVEQUAL", "EQEQ", "NOTEQ",
  "LT", "GT", "LTEQ", "GTEQ", "AND", "OR", "NOT", "PLUS", "MINUS", "MULT",
  "DIV", "MOD", "AMPERSAND", "INCREMENT", "DECREMENT", "ARROW", "RARROW",
  "KW_QUOTE", "HASH_FOR", "HASH_INDEX", "SHIFTLEFT", "SHIFTRIGHT",
  "SHIFTLEFTEQUAL", "SHIFTRIGHTEQUAL", "PIPE", "CARET", "BACKTICK",
  "TYPE_ANNOT_PREC", "UMINUS", "ADDRESSOF_PREC", "DEREFERENCE_PREC",
  "HIGH_PREC", "TYPE_PREC", "KW_IFX", "$accept", "program", "statements",
  "statement", "block", "block_stmts", "annotated_declaration",
  "declaration_with_vis", "bare_declaration", "declaration_body",
  "attributes_opt", "attribute_list", "attribute", "attr_id",
  "generic_params_opt", "generic_param_list", "generic_param",
  "import_statement", "import_list", "dotted_path", "operator_params_opt",
  "inheritance_opt", "struct_body_content", "struct_item_rest",
  "operator_symbol", "operator_generics_opt", "implements_opt",
  "interface_body_content", "interface_item_rest", "params", "param_list",
  "param", "super_expression", "define_declaration", "extern_params",
  "macro_declaration", "macro_rules", "macro_rule", "macro_param_list",
  "macro_param", "type_definition", "implements_block",
  "implements_body_content", "implements_item_rest", "special_declaration",
  "variable_declaration", "type", "type_no_annot", "base_type", "fn_type",
  "type_list", "pointer_type", "array_type", "primitive_type",
  "if_statement", "while_loop", "for_loop", "foreach_loop",
  "control_statement", "delete_statement", "try_catch_statement",
  "blame_statement", "return_statement", "expression_statement",
  "lambda_expression", "expression", "no_struct_expression",
  "static_method_call", "primary_no_struct", "literal", "arguments",
  "expression_list", "macro_arg_item", "macro_arg_list_body",
  "macro_arguments", "field_assignments", "visibility_opt", "enum_values",
  "enum_value", "prototype_literal", "prototype_elements", YY_NULLPTR
    };
    return yy_sname[yysymbol];
  }



  // parser::context.
  parser::context::context (const parser& yyparser, const symbol_type& yyla)
    : yyparser_ (yyparser)
    , yyla_ (yyla)
  {}

  int
  parser::context::expected_tokens (symbol_kind_type yyarg[], int yyargn) const
  {
    // Actual number of expected tokens
    int yycount = 0;

    const int yyn = yypact_[+yyparser_.yystack_[0].state];
    if (!yy_pact_value_is_default_ (yyn))
      {
        /* Start YYX at -YYN if negative to avoid negative indexes in
           YYCHECK.  In other words, skip the first -YYN actions for
           this state because they are default actions.  */
        const int yyxbegin = yyn < 0 ? -yyn : 0;
        // Stay within bounds of both yycheck and yytname.
        const int yychecklim = yylast_ - yyn + 1;
        const int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
        for (int yyx = yyxbegin; yyx < yyxend; ++yyx)
          if (yycheck_[yyx + yyn] == yyx && yyx != symbol_kind::S_YYerror
              && !yy_table_value_is_error_ (yytable_[yyx + yyn]))
            {
              if (!yyarg)
                ++yycount;
              else if (yycount == yyargn)
                return 0;
              else
                yyarg[yycount++] = YY_CAST (symbol_kind_type, yyx);
            }
      }

    if (yyarg && yycount == 0 && 0 < yyargn)
      yyarg[0] = symbol_kind::S_YYEMPTY;
    return yycount;
  }






  int
  parser::yy_syntax_error_arguments_ (const context& yyctx,
                                                 symbol_kind_type yyarg[], int yyargn) const
  {
    /* There are many possibilities here to consider:
       - If this state is a consistent state with a default action, then
         the only way this function was invoked is if the default action
         is an error action.  In that case, don't check for expected
         tokens because there are none.
       - The only way there can be no lookahead present (in yyla) is
         if this state is a consistent state with a default action.
         Thus, detecting the absence of a lookahead is sufficient to
         determine that there is no unexpected or expected token to
         report.  In that case, just report a simple "syntax error".
       - Don't assume there isn't a lookahead just because this state is
         a consistent state with a default action.  There might have
         been a previous inconsistent state, consistent state with a
         non-default action, or user semantic action that manipulated
         yyla.  (However, yyla is currently not documented for users.)
       - Of course, the expected token list depends on states to have
         correct lookahead information, and it depends on the parser not
         to perform extra reductions after fetching a lookahead from the
         scanner and before detecting a syntax error.  Thus, state merging
         (from LALR or IELR) and default reductions corrupt the expected
         token list.  However, the list is correct for canonical LR with
         one exception: it will still contain any token that will not be
         accepted due to an error action in a later state.
    */

    if (!yyctx.lookahead ().empty ())
      {
        if (yyarg)
          yyarg[0] = yyctx.token ();
        int yyn = yyctx.expected_tokens (yyarg ? yyarg + 1 : yyarg, yyargn - 1);
        return yyn + 1;
      }
    return 0;
  }

  // Generate an error message.
  std::string
  parser::yysyntax_error_ (const context& yyctx) const
  {
    // Its maximum.
    enum { YYARGS_MAX = 5 };
    // Arguments of yyformat.
    symbol_kind_type yyarg[YYARGS_MAX];
    int yycount = yy_syntax_error_arguments_ (yyctx, yyarg, YYARGS_MAX);

    char const* yyformat = YY_NULLPTR;
    switch (yycount)
      {
#define YYCASE_(N, S)                         \
        case N:                               \
          yyformat = S;                       \
        break
      default: // Avoid compiler warnings.
        YYCASE_ (0, YY_("syntax error"));
        YYCASE_ (1, YY_("syntax error, unexpected %s"));
        YYCASE_ (2, YY_("syntax error, unexpected %s, expecting %s"));
        YYCASE_ (3, YY_("syntax error, unexpected %s, expecting %s or %s"));
        YYCASE_ (4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
        YYCASE_ (5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
      }

    std::string yyres;
    // Argument number.
    std::ptrdiff_t yyi = 0;
    for (char const* yyp = yyformat; *yyp; ++yyp)
      if (yyp[0] == '%' && yyp[1] == 's' && yyi < yycount)
        {
          yyres += symbol_name (yyarg[yyi++]);
          ++yyp;
        }
      else
        yyres += *yyp;
    return yyres;
  }


  const short parser::yypact_ninf_ = -687;

  const signed char parser::yytable_ninf_ = -1;

  const short
  parser::yypact_[] =
  {
     870,     3,  -687,  -687,  -687,  -687,    73,   157,    49,   251,
      62,    62,   182,   211,   250,   268,  -687,    93,   214,   290,
     280,   292,   277,   310,  1113,    -1,     0,    24,    19,  1113,
     338,   294,   409,   428,   919,  1113,  1113,  -687,    84,   433,
     376,  1113,  1113,  1113,  1113,  1113,  1113,   310,   444,   870,
    -687,  -687,  -687,  -687,  -687,    20,  -687,  -687,  -687,  -687,
    -687,  -687,  -687,  -687,  -687,  -687,  -687,  -687,  -687,  -687,
    -687,  -687,  -687,  -687,  -687,  1557,  -687,  -687,  -687,  -687,
     367,   453,    16,   382,   383,   384,    25,   209,   423,  -687,
    1593,   468,  -687,  -687,   384,   417,   384,   426,  1113,   228,
     405,  -687,  -687,  1113,   870,   463,  1485,  1113,   453,   490,
    1113,   493,  -687,   429,   495,    50,   217,   448,  1301,  1301,
    1629,  1301,  1301,   384,   384,   227,   511,   452,   446,  -687,
    1668,  1704,    82,  2403,   451,   450,   521,   523,   525,  -687,
      38,   127,   127,   127,   127,  -687,  -687,  -687,  -687,  -687,
    -687,  -687,   483,  -687,    62,  1113,  1113,  -687,   527,  1113,
    1113,  1113,  1113,  1113,  1113,  1113,  1113,  1113,  1113,  1113,
    1113,  1113,  1113,   469,  1113,  1113,  1113,  1113,  1113,  -687,
    -687,  1113,  1113,  1301,   465,   168,   470,  1301,  1301,  1301,
     533,   476,   471,   481,  -687,   474,   544,   484,   488,  1743,
     548,   556,  1113,  1301,  1782,   870,   497,   501,  -687,  1113,
     507,   218,   362,   508,   528,  -687,  -687,   238,   575,  -687,
     586,   453,   505,   453,   506,  -687,  -687,   534,   509,  -687,
    -687,  -687,  -687,  -687,  -687,  -687,  -687,  1301,  1301,  1301,
    -687,   547,  1301,  1301,  1301,   240,  -687,   535,  -687,  -687,
    -687,  -687,   536,  -687,   539,   517,   519,   474,  1301,   537,
     524,    25,  -687,  1113,  -687,  1113,  -687,  1113,   541,   543,
     545,  -687,  -687,    77,   611,  -687,   554,  1818,  -687,  1854,
    2403,  2403,  2403,  2403,  2403,   480,   480,   380,   380,   380,
     380,   573,  2467,  1113,   233,   233,   127,   127,   127,   150,
     150,   531,  1113,  -687,   616,  1113,  -687,    58,   542,   546,
     551,    64,  -687,    25,   549,   550,   557,   552,   241,  -687,
    -687,    36,   258,  -687,   310,   558,   561,  1890,   553,   310,
    -687,   619,  1926,  -687,  -687,  1113,   453,  -687,  1113,   594,
     631,   569,  -687,   243,  1301,   245,  1301,   447,  1301,   579,
     256,   321,  -687,  -687,  -687,  -687,   574,  1113,  1113,   453,
    -687,   581,     4,   584,  -687,  1301,  1301,  -687,  2403,  1962,
    2403,   646,    25,    35,   648,  -687,   647,   592,  -687,  -687,
    1113,   593,   591,  2403,   595,   608,  1301,   204,   119,   596,
    1301,   533,  -687,   613,  1301,  1301,  -687,  1113,  -687,   544,
     145,   614,   618,   582,  -687,  -687,  -687,  1301,  1301,  1113,
     644,   654,   649,  -687,   621,   271,   624,   331,  -687,  -687,
    -687,    72,  -687,   -12,   588,   169,    -6,  -687,  -687,  -687,
    1113,   453,  1998,   273,   282,  1113,   638,  1301,  -687,  -687,
     617,  1113,   117,   207,  -687,   653,   511,   639,   656,   128,
     655,    25,  2435,  -687,  -687,  1113,  -687,  -687,  1113,   453,
     708,  -687,  1113,  1113,  -687,  -687,   634,   636,   118,   148,
    2403,  -687,  -687,   419,   645,   623,   626,   310,   642,   643,
    2037,  1155,   310,  1301,  -687,  -687,  -687,   657,   226,   667,
    -687,  -687,  1301,   337,  -687,  -687,  2073,   302,   668,  -687,
    -687,  2112,   650,   652,   152,   632,  2403,   731,  -687,   310,
     646,   651,    37,   658,   674,  -687,   672,   158,  2403,   679,
     305,   683,  2148,  2184,  1301,   310,  -687,  -687,   419,   185,
     310,   310,  -687,   196,   671,   310,   676,   685,    21,   919,
    1155,  1155,  1155,  1155,  1155,  1155,  1367,  -687,  -687,   688,
    -687,  -687,   453,  -687,  1301,  1301,  -687,  -687,   453,  -687,
    1301,   684,  -687,  1197,   680,  -687,  -687,  1301,   511,  1301,
    -687,   669,  -687,   419,  -687,  -687,  1113,  -687,  -687,   675,
    -687,    46,   670,   692,  1316,   723,  -687,  -687,  -687,  -687,
    -687,  1113,  1113,  -687,    18,    25,   308,   695,  1301,   698,
     355,   355,   355,   355,  -687,  -687,  1113,  1113,   761,  1155,
    1155,  1155,  1155,  1155,  1155,  1155,  1155,  1000,  1046,  1155,
    1155,   351,  1155,  1155,  1155,  1155,  1155,   686,  -687,  -687,
     659,  -687,   310,   318,  -687,  -687,   319,   121,  -687,   870,
    -687,  2403,  -687,   687,   696,  1301,   137,   704,   254,    -2,
     767,   760,  1316,   770,  -687,  1301,    25,   712,   719,  -687,
    -687,  -687,  -687,  -687,  -687,  -687,  -687,  -687,  -687,  -687,
    -687,  -687,  -687,  -687,  -687,  -687,  -687,  -687,  -687,  -687,
    -687,   346,   721,   729,  2220,  2256,  1301,   725,   453,   707,
     453,   730,   709,   733,  2292,  -687,  1406,  1445,  1445,  1445,
    1445,  1445,   269,   269,  1155,   461,  1155,   461,   461,   461,
    1113,  1113,  1113,   516,   516,   355,   355,   355,  1155,  1155,
    -687,  -687,  -687,   732,  1521,   310,   737,   720,   803,   796,
    1316,  -687,  -687,  -687,  -687,    25,  1301,   384,   806,   721,
     754,   741,   766,   827,   752,  -687,  -687,   747,   772,   775,
    -687,  -687,   125,   749,   320,  1301,   345,   453,  1301,  -687,
    -687,  1155,   461,   461,  2328,  -687,   768,   778,   777,   791,
    2499,   726,  -687,  -687,  -687,   310,   384,   861,   721,   802,
     779,   805,   384,   772,   807,   800,   801,   808,  -687,   533,
      25,   786,   810,    78,  1301,  -687,   138,  -687,   349,   794,
     309,  1113,  1113,  -687,  -687,  -687,  -687,   828,   384,   772,
      56,    75,    25,   836,   847,   310,  -687,  -687,  -687,   144,
     837,  1301,  -687,   815,   843,  -687,   811,  2403,  -687,    25,
     846,   829,  1301,  -687,  -687,  1113,   850,    25,   830,   831,
    -687,  -687,  -687,   832,   310,   453,  1239,   855,    25,  1301,
     840,  2367,   842,   867,  1301,  1301,   865,  -687,   357,  -687,
    1445,   849,   874,   852,   310,  -687,  1301,   856,   860,   862,
    -687,  -687,  1301,   864,   310,  -687,   872,  1301,  -687,   361,
     879,  1301,  -687,   310,   880,  -687,  -687,   310,   884,  -687,
     310,  -687,   310,  -687,  -687
  };

  const short
  parser::yydefact_[] =
  {
       3,   306,   319,   320,   321,   322,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   323,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   122,     0,   325,    24,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     2,
       5,     6,     7,     8,    31,   340,    45,    11,   318,     9,
      10,    41,    23,    22,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,   251,     0,   316,   250,   307,   309,
       0,   337,     0,     0,     0,    52,   122,   306,     0,   212,
       0,     0,    29,    30,    52,     0,    52,     0,     0,     0,
       0,   205,   206,     0,    27,     0,     0,   325,   337,     0,
     325,     0,    64,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    52,    52,   306,     0,     0,   121,   124,
       0,     0,     0,   327,     0,   324,     0,     0,     0,   310,
       0,   238,   237,   240,   239,   241,   242,   317,     1,     4,
     338,   339,     0,    44,     0,   325,     0,   213,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   245,
     246,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   211,    69,   343,     0,     0,     0,
       0,     0,     0,     0,     0,    26,     0,     0,   209,     0,
       0,     0,     0,     0,     0,    57,    63,     0,     0,    58,
       0,   337,     0,   337,   170,   174,   175,     0,   176,   192,
     194,   195,   199,   196,   197,   198,   193,     0,     0,     0,
     165,     0,     0,     0,     0,     0,   163,   166,   179,   167,
     168,   169,     0,   207,     0,     0,     0,    69,     0,     0,
       0,     0,   308,     0,   346,     0,   311,     0,     0,     0,
       0,    49,    50,     0,     0,    28,     0,     0,   244,     0,
     217,   218,   219,   220,   221,   224,   225,   226,   227,   228,
     229,   223,   222,   325,   230,   231,   232,   233,   234,   235,
     236,     0,     0,   252,     0,   325,   186,     0,     0,     0,
      55,     0,    54,   122,     0,     0,     0,   344,     0,   342,
     115,     0,     0,   139,     0,     0,     0,     0,     0,     0,
      25,     0,     0,   130,   127,   325,   337,   315,   325,     0,
       0,     0,    65,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   181,   189,   188,   187,     0,     0,     0,   337,
     313,     0,     0,     0,   125,     0,     0,   123,   348,     0,
     326,   145,   122,   135,     0,    47,     0,     0,   243,   247,
       0,     0,     0,   336,     0,     0,     0,     0,     0,     0,
       0,     0,    51,     0,     0,     0,    71,     0,    36,     0,
      43,     0,     0,     0,   137,   138,   202,     0,     0,     0,
       0,   200,     0,   210,     0,     0,     0,     0,    62,    59,
     254,     0,   259,     0,     0,     0,     0,   180,   173,   190,
       0,   337,     0,     0,     0,     0,     0,     0,    71,   126,
       0,     0,   146,     0,   144,     0,   134,   133,     0,     0,
       0,   122,   248,   249,   154,     0,   303,   185,   325,   337,
       0,    40,     0,     0,    56,    53,     0,     0,     0,    43,
     345,   341,    35,   340,    42,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   129,   128,   305,     0,     0,     0,
     171,   172,     0,     0,   177,   178,     0,     0,     0,   164,
     256,     0,     0,     0,    43,     0,   347,     0,   147,     0,
       0,     0,     0,     0,     0,    46,     0,    43,   335,     0,
       0,     0,     0,     0,     0,     0,    68,    34,   340,     0,
       0,     0,   142,     0,     0,     0,   306,     0,     0,   122,
       0,     0,     0,     0,     0,     0,     0,   295,   201,     0,
      60,    61,   337,   184,     0,     0,   191,   257,   337,   312,
       0,     0,    37,     0,   148,   136,   143,     0,   132,     0,
      48,     0,   152,   340,   314,   253,   325,    38,    39,     0,
     214,     0,     0,     0,     0,     0,   117,   114,   140,   141,
     162,     0,     0,   203,     0,   122,     0,     0,     0,     0,
     281,   280,   283,   282,   284,   285,   325,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   288,   289,
       0,   204,     0,     0,   182,   183,     0,     0,   150,    27,
     215,   216,   149,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    70,     0,   122,     0,     0,   104,
      88,    89,    90,    91,    92,    93,    95,    97,   103,    83,
      84,    85,    86,    87,    94,    99,   100,   101,   102,    96,
      98,     0,   111,     0,     0,     0,     0,     0,   337,     0,
     337,     0,     0,     0,     0,   287,     0,   260,   261,   262,
     263,   264,   267,   268,     0,   269,     0,   270,   271,   272,
     334,   334,   334,   273,   274,   275,   276,   277,     0,     0,
     208,   255,   258,     0,     0,     0,     0,     0,     0,     0,
       0,   153,   304,    33,    32,   122,     0,    52,     0,   111,
       0,     0,     0,     0,   105,   108,   107,     0,    67,     0,
     160,   161,     0,     0,     0,     0,     0,   337,     0,   286,
     290,     0,   278,   279,   328,   331,   332,     0,     0,     0,
     266,   265,   151,   158,   131,     0,    52,     0,   111,     0,
       0,     0,    52,    67,     0,     0,     0,     0,   106,     0,
     122,     0,     0,     0,     0,   299,     0,   302,     0,     0,
     291,     0,   333,   292,   293,   294,   159,     0,    52,    67,
       0,    73,   122,     0,   113,     0,   116,   119,   109,     0,
       0,     0,   120,     0,     0,   301,     0,   329,   330,   122,
       0,     0,     0,    80,    72,     0,     0,   122,     0,     0,
      82,   110,    66,     0,     0,   337,     0,     0,   122,     0,
       0,    75,     0,     0,     0,     0,     0,   296,     0,   297,
     298,     0,     0,     0,     0,    74,     0,     0,     0,     0,
     118,   300,     0,     0,     0,    81,     0,     0,   112,     0,
       0,     0,   157,     0,     0,    79,    78,     0,     0,    76,
       0,   155,     0,    77,   156
  };

  const short
  parser::yypgoto_[] =
  {
    -687,  -687,   948,   -24,   -47,  -687,  -687,  -687,  -687,    -4,
    -392,  -383,   -51,  -687,   -91,   162,   566,  -687,  -687,  -324,
    -668,   715,   514,  -687,  -623,  -686,  -687,  -687,  -687,   -72,
     601,  -251,  -687,  -687,  -687,  -687,  -687,   660,  -687,   466,
    -687,  -687,  -687,  -687,  -687,  -687,  -106,  -687,  -687,  -687,
    -220,  -687,  -687,  -687,  -687,  -687,  -687,  -687,  -687,  -687,
    -687,  -687,  -687,  -687,  -687,     2,   237,  -687,    83,  -687,
     -87,   603,   173,  -687,  -252,  -107,  -464,  -687,   578,  -687,
    -687
  };

  const short
  parser::yydefgoto_[] =
  {
       0,    48,   205,    50,   105,   206,    51,    52,    53,    54,
     528,    55,    56,   273,   191,   311,   312,    57,   217,   115,
     791,   316,   469,   654,   682,   748,   839,   400,   587,   127,
     128,   129,    58,    59,   448,    60,   322,   323,   443,   444,
      61,    62,   517,   731,    63,   202,   306,   246,   247,   248,
     307,   249,   250,   251,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,   133,   546,    76,    77,    78,
     134,   135,   765,   766,   767,   185,   154,   318,   319,    79,
     132
  };

  const short
  parser::yytable_[] =
  {
     147,   211,    75,   195,   153,   197,    92,    93,   473,   529,
     367,    90,   245,   252,   193,   254,   255,   474,   350,   186,
     210,   186,   116,   213,   596,   149,   106,   112,   192,   739,
     113,   120,   256,   257,   150,   151,   130,   131,   192,   401,
     192,   271,   402,   141,   142,   143,   144,   145,   146,   649,
     449,    75,    85,   783,   117,    80,   597,   436,   650,   386,
     735,   107,   110,   108,   581,   386,   651,    81,   276,   109,
     111,     6,     7,    82,    91,   490,    83,   301,    12,    13,
      14,   494,   308,   309,   736,   118,   474,   218,   114,   272,
     437,   652,   809,   488,   152,   491,    40,   328,   126,   403,
     199,   495,   187,   136,   686,   204,    75,   778,   446,   646,
     568,    86,    32,    33,   343,   814,   345,   119,   219,   598,
     104,   474,   220,   653,   421,   573,   423,   425,   426,   386,
     137,   349,   138,   351,   474,   391,   353,   354,   355,   374,
     458,   831,   832,   386,   375,   387,   834,   264,   460,   728,
     275,   392,   364,   265,   835,    98,   376,   729,   277,   489,
      84,   279,   280,   281,   282,   283,   284,   285,   286,   287,
     288,   289,   290,   291,   292,   468,   294,   295,   296,   297,
     298,   149,   730,   299,   300,    94,   507,   461,   582,   386,
     508,   514,   386,   156,     6,     7,   386,    91,   462,   158,
     220,    12,    13,    14,   327,   526,   381,    75,   723,   386,
     472,   332,   793,   527,    95,   391,   156,   562,   385,   173,
     583,    40,   158,   572,    40,   824,   179,   180,    40,   415,
     584,   841,   493,   303,    40,    32,    33,   200,   201,   304,
     386,   393,   173,   174,   175,   176,   177,   178,   414,   179,
     180,   416,   434,    96,    87,     2,     3,     4,     5,   439,
     440,   367,   585,    88,   590,   368,   458,   369,   459,   370,
     509,    97,    16,    81,   460,   591,    99,   406,   510,    82,
     457,   221,   411,   334,   464,    25,    26,   222,   467,   304,
      28,    81,    30,   100,   551,    31,   258,    82,   220,   156,
     445,   478,   479,   339,   383,   158,   398,   356,   420,   340,
     422,   357,   399,    34,   304,    35,   304,    36,   104,    89,
     321,   428,   733,   404,   497,   173,    39,   386,   176,   177,
     178,   503,   179,   180,   112,   607,   485,   487,   499,   103,
     637,   608,   304,    41,   267,    42,    43,   500,   101,    44,
      45,    46,   520,   304,    47,   617,   618,   619,   620,   432,
     102,   621,   622,   623,   624,   625,   626,   557,   628,   629,
     575,   519,   688,   304,   104,   607,   304,   549,   689,   516,
     122,   608,   452,   721,   722,   795,   553,   609,   429,   304,
     304,   304,   430,   615,   616,   617,   618,   619,   620,   470,
     121,   621,   622,   623,   624,   625,   626,   627,   628,   629,
     797,   480,   123,   710,   825,   711,   304,   712,   579,   630,
     304,   607,   871,   153,   335,   104,   336,   608,   304,   885,
     532,   124,   496,   150,   151,   548,   139,   501,   554,   555,
     745,   746,   140,   506,   148,   633,   156,   621,   634,   635,
     224,   636,   158,   183,   628,   629,   184,   518,   225,   768,
     769,   643,   565,   644,   522,   523,   752,   599,   188,   189,
     190,    85,   173,   174,   175,   176,   177,   178,   580,   179,
     180,   196,   226,   588,   589,    86,   181,   182,   593,   647,
     198,   203,   691,   212,   227,   207,   214,   215,   216,   631,
     228,   229,   230,   231,   232,   233,   234,   235,   236,   237,
     424,   238,   223,   239,   259,   260,   640,   261,   266,   693,
     240,   267,   241,   687,   268,   586,   269,   607,   270,   274,
     278,   293,   305,   608,   302,   796,   310,   242,   313,   727,
     258,   130,   243,   315,   314,   244,   156,   317,   320,   741,
     321,   325,   158,   621,   622,   623,   624,   625,   626,   326,
     628,   629,   330,   331,   547,   641,   167,   168,   169,   170,
     333,   337,   173,   174,   175,   176,   177,   178,   341,   179,
     180,   754,   607,   756,   742,   720,   181,   182,   608,   342,
     338,   344,   346,   684,   685,   348,   347,   352,   362,   358,
     359,   734,   360,   371,   361,   372,   365,   373,   621,   694,
     366,   624,   625,   626,   377,   628,   629,   378,   382,   384,
     390,   396,   412,   547,   547,   547,   547,   547,   547,   388,
     780,   397,   417,   389,   418,   394,   395,   419,   431,   156,
     410,   724,   427,   435,   407,   158,   781,   408,   438,   442,
     798,   112,   799,   450,   451,   454,   453,   165,   166,   167,
     168,   169,   170,   779,   455,   173,   174,   175,   176,   177,
     178,   456,   179,   180,   481,   463,   466,   475,   773,   181,
     182,   476,   482,   477,   484,   807,   483,   486,   823,   492,
     502,   813,   547,   547,   547,   547,   547,   547,   547,   547,
     547,   547,   547,   547,   505,   547,   547,   547,   547,   547,
     512,   521,   764,   764,   764,   843,   511,   830,   820,   513,
     524,    40,   515,   525,   530,   550,   850,   531,   806,   533,
     534,   552,   558,   563,   564,   571,   560,   567,   858,   561,
     836,   570,   574,   863,   569,   576,   594,   595,   868,   869,
     592,   632,   638,   642,   656,   645,   655,   847,   683,   690,
     876,   692,   648,   833,   695,   853,   880,   732,   840,   719,
     737,   884,   738,   740,   725,   888,   862,   600,   601,   602,
     603,   604,   605,   726,   718,   743,   744,   547,   753,   547,
     747,   749,   607,   755,   757,   758,   759,   857,   608,   859,
     772,   547,   547,   827,   764,   774,   776,   775,   777,   782,
     615,   616,   617,   618,   619,   620,   784,   875,   621,   622,
     623,   624,   625,   626,   627,   628,   629,   882,   785,   786,
     787,   788,   886,   789,   790,   794,   889,   851,   792,   802,
     891,   803,   804,   893,   547,   894,   696,   697,   698,   699,
     700,   701,   702,   703,   705,   707,   708,   709,   805,   713,
     714,   715,   716,   717,   808,   810,   811,   812,   816,   817,
     815,   818,   821,     1,     2,     3,     4,     5,   822,     6,
       7,   826,     8,     9,    10,    11,    12,    13,    14,    15,
     829,    16,    17,    18,    19,    20,    21,    22,   837,   838,
     842,    23,   844,    24,    25,    26,    27,   845,   848,    28,
      29,    30,   846,   852,    31,   849,   854,   855,   861,   856,
      32,    33,   125,     2,     3,     4,     5,   864,   866,   547,
     867,    88,    34,   870,    35,   872,    36,   873,    37,   874,
      16,   762,   877,   763,    38,    39,    40,   878,    49,   879,
     881,   819,   504,    25,    26,   770,   771,   465,    28,   883,
      30,   433,    41,    31,    42,    43,   887,   890,    44,    45,
      46,   892,   363,    47,   447,   828,   566,   471,     0,     0,
       0,    34,   405,    35,     0,    36,     0,     0,     0,     0,
       0,     0,   126,     0,    39,     0,     0,     0,   800,     0,
       0,     0,     0,   536,     2,     3,     4,     5,     0,     0,
       0,    41,   537,    42,    43,     0,     0,    44,    45,    46,
       0,    16,    47,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    25,    26,     0,     0,     0,   538,
       0,    30,     0,     0,    31,     0,     0,     0,     0,   536,
       2,     3,     4,     5,     0,     0,     0,     0,   537,     0,
       0,     0,   539,     0,    35,     0,    36,    16,     0,     0,
       0,     0,     0,     0,     0,    39,     0,     0,     0,     0,
      25,    26,     0,   860,     0,   538,   704,    30,     0,     0,
      31,     0,   540,     0,   541,   542,     0,     0,   543,   544,
     545,     0,     0,    47,     0,     0,     0,     0,   539,     0,
      35,     0,    36,     0,     0,     0,    87,     2,     3,     4,
       5,    39,     0,     0,     0,    88,     0,     0,     0,     0,
       0,     0,     0,   706,    16,     0,     0,     0,   540,     0,
     541,   542,     0,     0,   543,   544,   545,    25,    26,    47,
       0,     0,    28,     0,    30,     0,     0,    31,   536,     2,
       3,     4,     5,     0,     0,     0,     0,   537,     0,     0,
       0,     0,     0,     0,     0,    34,    16,    35,     0,    36,
       0,     0,     0,     0,     0,     0,     0,     0,    39,    25,
      26,     0,     0,     0,   538,     0,    30,     0,     0,    31,
      87,     2,     3,     4,     5,    41,     0,    42,    43,    88,
       0,    44,    45,    46,     0,     0,    47,   539,    16,    35,
       0,    36,     0,     0,     0,     0,     0,     0,     0,     0,
      39,    25,    26,     0,     0,     0,    28,     0,    30,     0,
       0,    31,   536,     2,     3,     4,     5,   540,     0,   541,
     542,   537,     0,   543,   544,   545,     0,     0,    47,    34,
      16,   639,     0,    36,     0,     0,     0,     0,     0,     0,
       0,     0,    39,    25,    26,     0,     0,     0,   538,     0,
      30,     0,     0,    31,     0,     0,     0,     0,     0,    41,
       0,    42,    43,     0,     0,    44,    45,    46,     0,     0,
      47,   539,     0,   639,   224,    36,     0,     0,     0,     0,
       0,     0,   225,     0,    39,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   540,     0,   541,   542,     0,   226,   543,   544,   545,
       0,     0,    47,     0,     0,     0,     0,     0,   227,     0,
       0,     0,     0,     0,   228,   229,   230,   231,   232,   233,
     234,   235,   236,   237,     0,   238,     0,   239,     0,     0,
       0,     0,     0,     0,   240,     0,   241,     0,   657,     0,
       0,     0,   658,     0,     0,     0,     0,     0,     0,     0,
       0,   242,     0,     0,     0,   659,   243,     0,     0,   244,
     660,   661,   662,   663,   664,   665,   666,   667,   668,   669,
     670,   671,   672,   673,   674,     0,     0,     0,     0,     0,
       0,     0,   675,   676,   677,   678,   679,   680,   681,   606,
       0,   104,     0,   607,     0,     0,     0,     0,     0,   608,
       0,     0,     0,     0,     0,   609,   610,   611,   612,   613,
     614,   615,   616,   617,   618,   619,   620,     0,     0,   621,
     622,   623,   624,   625,   626,   627,   628,   629,   606,     0,
       0,     0,   607,     0,     0,   761,     0,   630,   608,     0,
       0,     0,     0,     0,   609,   610,   611,   612,   613,   614,
     615,   616,   617,   618,   619,   620,     0,     0,   621,   622,
     623,   624,   625,   626,   627,   628,   629,   606,     0,     0,
       0,   607,     0,     0,     0,     0,   630,   608,     0,     0,
       0,     0,     0,   609,   610,   611,   612,   613,   614,   615,
     616,   617,   618,   619,   620,     0,     0,   621,   622,   623,
     624,   625,   626,   627,   628,   629,     0,   155,     0,     0,
       0,   156,     0,   208,     0,   630,   209,   158,     0,     0,
       0,     0,     0,   159,   160,   161,   162,   163,   164,   165,
     166,   167,   168,   169,   170,   171,   172,   173,   174,   175,
     176,   177,   178,   155,   179,   180,     0,   156,     0,   157,
     263,   181,   182,   158,     0,     0,     0,     0,     0,   159,
     160,   161,   162,   163,   164,   165,   166,   167,   168,   169,
     170,   171,   172,   173,   174,   175,   176,   177,   178,   155,
     179,   180,     0,   156,     0,   157,     0,   181,   182,   158,
       0,     0,     0,     0,     0,   159,   160,   161,   162,   163,
     164,   165,   166,   167,   168,   169,   170,   171,   172,   173,
     174,   175,   176,   177,   178,   155,   179,   180,     0,   156,
       0,   194,     0,   181,   182,   158,     0,     0,     0,     0,
       0,   159,   160,   161,   162,   163,   164,   165,   166,   167,
     168,   169,   170,   171,   172,   173,   174,   175,   176,   177,
     178,   155,   179,   180,     0,   156,     0,   253,     0,   181,
     182,   158,     0,     0,     0,     0,     0,   159,   160,   161,
     162,   163,   164,   165,   166,   167,   168,   169,   170,   171,
     172,   173,   174,   175,   176,   177,   178,     0,   179,   180,
     155,   262,     0,     0,   156,   181,   182,     0,     0,     0,
     158,     0,     0,     0,     0,     0,   159,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,   171,   172,
     173,   174,   175,   176,   177,   178,   155,   179,   180,     0,
     156,     0,     0,   263,   181,   182,   158,     0,     0,     0,
       0,     0,   159,   160,   161,   162,   163,   164,   165,   166,
     167,   168,   169,   170,   171,   172,   173,   174,   175,   176,
     177,   178,     0,   179,   180,   155,   324,     0,     0,   156,
     181,   182,     0,     0,     0,   158,     0,     0,     0,     0,
       0,   159,   160,   161,   162,   163,   164,   165,   166,   167,
     168,   169,   170,   171,   172,   173,   174,   175,   176,   177,
     178,     0,   179,   180,   155,   329,     0,     0,   156,   181,
     182,     0,     0,     0,   158,     0,     0,     0,     0,     0,
     159,   160,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   170,   171,   172,   173,   174,   175,   176,   177,   178,
     155,   179,   180,     0,   156,   379,     0,     0,   181,   182,
     158,     0,     0,     0,     0,     0,   159,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,   171,   172,
     173,   174,   175,   176,   177,   178,   155,   179,   180,     0,
     156,     0,     0,   380,   181,   182,   158,     0,     0,     0,
       0,     0,   159,   160,   161,   162,   163,   164,   165,   166,
     167,   168,   169,   170,   171,   172,   173,   174,   175,   176,
     177,   178,   155,   179,   180,     0,   156,     0,   409,     0,
     181,   182,   158,     0,     0,     0,     0,     0,   159,   160,
     161,   162,   163,   164,   165,   166,   167,   168,   169,   170,
     171,   172,   173,   174,   175,   176,   177,   178,   155,   179,
     180,     0,   156,     0,   413,     0,   181,   182,   158,     0,
       0,     0,     0,     0,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   155,   179,   180,     0,   156,     0,
       0,   441,   181,   182,   158,     0,     0,     0,     0,     0,
     159,   160,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   170,   171,   172,   173,   174,   175,   176,   177,   178,
     155,   179,   180,     0,   156,   498,     0,     0,   181,   182,
     158,     0,     0,     0,     0,     0,   159,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,   171,   172,
     173,   174,   175,   176,   177,   178,     0,   179,   180,   155,
     535,     0,     0,   156,   181,   182,     0,     0,     0,   158,
       0,     0,     0,     0,     0,   159,   160,   161,   162,   163,
     164,   165,   166,   167,   168,   169,   170,   171,   172,   173,
     174,   175,   176,   177,   178,   155,   179,   180,     0,   156,
     556,     0,     0,   181,   182,   158,     0,     0,     0,     0,
       0,   159,   160,   161,   162,   163,   164,   165,   166,   167,
     168,   169,   170,   171,   172,   173,   174,   175,   176,   177,
     178,     0,   179,   180,   155,   559,     0,     0,   156,   181,
     182,     0,     0,     0,   158,     0,     0,     0,     0,     0,
     159,   160,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   170,   171,   172,   173,   174,   175,   176,   177,   178,
     155,   179,   180,     0,   156,     0,   577,     0,   181,   182,
     158,     0,     0,     0,     0,     0,   159,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,   171,   172,
     173,   174,   175,   176,   177,   178,   155,   179,   180,     0,
     156,     0,   578,     0,   181,   182,   158,     0,     0,     0,
       0,     0,   159,   160,   161,   162,   163,   164,   165,   166,
     167,   168,   169,   170,   171,   172,   173,   174,   175,   176,
     177,   178,   155,   179,   180,     0,   156,     0,   750,     0,
     181,   182,   158,     0,     0,     0,     0,     0,   159,   160,
     161,   162,   163,   164,   165,   166,   167,   168,   169,   170,
     171,   172,   173,   174,   175,   176,   177,   178,   155,   179,
     180,     0,   156,     0,   751,     0,   181,   182,   158,     0,
       0,     0,     0,     0,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   155,   179,   180,     0,   156,   760,
       0,     0,   181,   182,   158,     0,     0,     0,     0,     0,
     159,   160,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   170,   171,   172,   173,   174,   175,   176,   177,   178,
     155,   179,   180,     0,   156,     0,     0,   801,   181,   182,
     158,     0,     0,     0,     0,     0,   159,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,   171,   172,
     173,   174,   175,   176,   177,   178,     0,   179,   180,   155,
       0,     0,     0,   156,   181,   182,     0,     0,   865,   158,
       0,     0,     0,     0,     0,   159,   160,   161,   162,   163,
     164,   165,   166,   167,   168,   169,   170,   171,   172,   173,
     174,   175,   176,   177,   178,   155,   179,   180,     0,   156,
       0,     0,     0,   181,   182,   158,     0,     0,     0,     0,
       0,   159,   160,   161,   162,   163,   164,   165,   166,   167,
     168,   169,   170,   171,   172,   173,   174,   175,   176,   177,
     178,   156,   179,   180,     0,     0,     0,   158,     0,   181,
     182,     0,     0,   159,     0,     0,     0,     0,     0,   165,
     166,   167,   168,   169,   170,   171,   172,   173,   174,   175,
     176,   177,   178,   156,   179,   180,     0,     0,     0,   158,
       0,   181,   182,     0,     0,     0,     0,     0,     0,     0,
       0,   165,   166,   167,   168,   169,   170,   171,     0,   173,
     174,   175,   176,   177,   178,   607,   179,   180,     0,     0,
       0,   608,     0,   181,   182,     0,     0,     0,     0,     0,
       0,     0,     0,   615,   616,   617,   618,   619,   620,     0,
       0,   621,   622,   623,   624,   625,   626,     0,   628,   629
  };

  const short
  parser::yycheck_[] =
  {
      47,   108,     0,    94,    55,    96,    10,    11,   400,   473,
     261,     9,   118,   119,    86,   121,   122,   400,   238,     3,
     107,     3,     3,   110,     3,    49,    24,     3,     3,   652,
       6,    29,   123,   124,    14,    15,    34,    35,     3,     3,
       3,     3,     6,    41,    42,    43,    44,    45,    46,     3,
     374,    49,     3,   739,    35,    52,    35,    53,    12,    71,
      62,    62,    62,    64,   528,    71,    20,    64,   155,    70,
      70,     9,    10,    70,    12,    87,     3,   183,    16,    17,
      18,    87,   188,   189,    86,    66,   469,    37,    64,    51,
      86,    45,   778,   417,    74,   107,    76,   203,    73,    63,
      98,   107,    86,    19,    86,   103,   104,   730,    73,   573,
      73,    62,    50,    51,   221,   783,   223,    98,    68,    98,
      64,   504,    72,    77,   344,   517,   346,   347,   348,    71,
      46,   237,    48,   239,   517,    71,   242,   243,   244,    62,
      62,   809,    86,    71,    67,    87,    71,    65,    70,    12,
     154,    87,   258,    71,    79,    62,    79,    20,   156,    87,
       3,   159,   160,   161,   162,   163,   164,   165,   166,   167,
     168,   169,   170,   171,   172,   395,   174,   175,   176,   177,
     178,   205,    45,   181,   182,     3,    69,    68,     3,    71,
      73,    63,    71,    66,     9,    10,    71,    12,    79,    72,
      72,    16,    17,    18,   202,    87,   293,   205,    87,    71,
      65,   209,    87,    65,     3,    71,    66,    65,   305,    92,
      35,    76,    72,    65,    76,    87,    99,   100,    76,   336,
      45,    87,    63,    65,    76,    50,    51,     9,    10,    71,
      71,   313,    92,    93,    94,    95,    96,    97,   335,    99,
     100,   338,   359,     3,     3,     4,     5,     6,     7,   365,
     366,   512,    77,    12,    68,   263,    62,   265,    64,   267,
      63,     3,    21,    64,    70,    79,    62,   324,    71,    70,
     386,    64,   329,    65,   390,    34,    35,    70,   394,    71,
      39,    64,    41,     3,    68,    44,    69,    70,    72,    66,
     372,   407,   408,    65,   302,    72,    65,    67,    65,    71,
      65,    71,    71,    62,    71,    64,    71,    66,    64,    68,
      62,    65,    68,    65,   431,    92,    75,    71,    95,    96,
      97,   437,    99,   100,     3,    66,    65,     6,    65,    62,
     560,    72,    71,    92,    71,    94,    95,    65,    68,    98,
      99,   100,   459,    71,   103,    86,    87,    88,    89,   357,
      68,    92,    93,    94,    95,    96,    97,    65,    99,   100,
      65,   458,    64,    71,    64,    66,    71,   483,    70,   451,
      86,    72,   380,    65,    65,    65,   492,    78,    67,    71,
      71,    71,    71,    84,    85,    86,    87,    88,    89,   397,
      62,    92,    93,    94,    95,    96,    97,    98,    99,   100,
      65,   409,     3,    62,    65,    64,    71,    66,   524,   110,
      71,    66,    65,   474,    62,    64,    64,    72,    71,    68,
     477,     3,   430,    14,    15,   482,     3,   435,   101,   102,
      94,    95,    66,   441,     0,   552,    66,    92,   554,   555,
       3,   558,    72,    86,    99,   100,     3,   455,    11,   711,
     712,   567,   509,   569,   462,   463,   686,   539,    86,    86,
      86,     3,    92,    93,    94,    95,    96,    97,   525,    99,
     100,    64,    35,   530,   531,    62,   106,   107,   535,   576,
      64,    86,   598,     3,    47,    32,     3,    68,     3,   546,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    64,    66,     3,    63,   563,    71,    67,   606,
      73,    71,    75,   595,     3,   529,     3,    66,     3,    46,
       3,    62,    62,    72,    69,   755,     3,    90,    62,   645,
      69,   539,    95,    69,    63,    98,    66,     3,    64,   655,
      62,     3,    72,    92,    93,    94,    95,    96,    97,     3,
      99,   100,    65,    62,   481,   563,    86,    87,    88,    89,
      63,    63,    92,    93,    94,    95,    96,    97,     3,    99,
     100,   688,    66,   690,   656,   632,   106,   107,    72,     3,
      62,    86,    86,   591,   592,    86,    62,    50,    79,    64,
      64,   648,    63,    62,    87,    62,    69,    62,    92,   607,
      86,    95,    96,    97,     3,    99,   100,    63,    87,     3,
      69,    64,     3,   540,   541,   542,   543,   544,   545,    87,
     736,    79,    38,    87,     3,    86,    86,    68,    64,    66,
      87,   639,    63,    62,    86,    72,   737,    86,    64,     3,
     757,     3,   758,     6,    62,    64,    63,    84,    85,    86,
      87,    88,    89,   735,    69,    92,    93,    94,    95,    96,
      97,    63,    99,   100,    30,    79,    63,    63,   725,   106,
     107,    63,    28,   101,    63,   776,    37,    63,   794,   101,
      52,   782,   609,   610,   611,   612,   613,   614,   615,   616,
     617,   618,   619,   620,    87,   622,   623,   624,   625,   626,
      71,     3,   710,   711,   712,   821,    63,   808,   790,    63,
      86,    76,    67,    87,   101,    68,   832,   101,   775,    87,
      87,    64,    64,   101,     3,    63,    86,    86,   845,    87,
     812,    67,    63,   849,    86,    62,    70,    62,   854,   855,
      79,    63,    68,    73,    62,    86,    86,   829,    35,    64,
     866,    63,    87,   810,     3,   837,   872,    63,   815,   110,
       3,   877,    12,     3,    87,   881,   848,   540,   541,   542,
     543,   544,   545,    87,    98,    73,    67,   704,    63,   706,
      69,    62,    66,    86,    64,    86,    63,   844,    72,   846,
      68,   718,   719,   801,   802,    68,     3,    87,    12,     3,
      84,    85,    86,    87,    88,    89,    62,   864,    92,    93,
      94,    95,    96,    97,    98,    99,   100,   874,    87,    63,
       3,    79,   879,    86,    62,    86,   883,   835,    63,    71,
     887,    63,    65,   890,   761,   892,   609,   610,   611,   612,
     613,   614,   615,   616,   617,   618,   619,   620,    67,   622,
     623,   624,   625,   626,     3,    63,    87,    62,    68,    68,
      63,    63,    86,     3,     4,     5,     6,     7,    68,     9,
      10,    87,    12,    13,    14,    15,    16,    17,    18,    19,
      62,    21,    22,    23,    24,    25,    26,    27,    62,    52,
      63,    31,    87,    33,    34,    35,    36,    64,    62,    39,
      40,    41,   101,    63,    44,    86,    86,    86,    63,    87,
      50,    51,     3,     4,     5,     6,     7,    87,    86,   846,
      63,    12,    62,    68,    64,    86,    66,    63,    68,    87,
      21,   704,    86,   706,    74,    75,    76,    87,     0,    87,
      86,   789,   438,    34,    35,   718,   719,   391,    39,    87,
      41,   358,    92,    44,    94,    95,    87,    87,    98,    99,
     100,    87,   257,   103,   373,   802,   510,   399,    -1,    -1,
      -1,    62,   322,    64,    -1,    66,    -1,    -1,    -1,    -1,
      -1,    -1,    73,    -1,    75,    -1,    -1,    -1,   761,    -1,
      -1,    -1,    -1,     3,     4,     5,     6,     7,    -1,    -1,
      -1,    92,    12,    94,    95,    -1,    -1,    98,    99,   100,
      -1,    21,   103,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    34,    35,    -1,    -1,    -1,    39,
      -1,    41,    -1,    -1,    44,    -1,    -1,    -1,    -1,     3,
       4,     5,     6,     7,    -1,    -1,    -1,    -1,    12,    -1,
      -1,    -1,    62,    -1,    64,    -1,    66,    21,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    75,    -1,    -1,    -1,    -1,
      34,    35,    -1,   846,    -1,    39,    86,    41,    -1,    -1,
      44,    -1,    92,    -1,    94,    95,    -1,    -1,    98,    99,
     100,    -1,    -1,   103,    -1,    -1,    -1,    -1,    62,    -1,
      64,    -1,    66,    -1,    -1,    -1,     3,     4,     5,     6,
       7,    75,    -1,    -1,    -1,    12,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    87,    21,    -1,    -1,    -1,    92,    -1,
      94,    95,    -1,    -1,    98,    99,   100,    34,    35,   103,
      -1,    -1,    39,    -1,    41,    -1,    -1,    44,     3,     4,
       5,     6,     7,    -1,    -1,    -1,    -1,    12,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    62,    21,    64,    -1,    66,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    75,    34,
      35,    -1,    -1,    -1,    39,    -1,    41,    -1,    -1,    44,
       3,     4,     5,     6,     7,    92,    -1,    94,    95,    12,
      -1,    98,    99,   100,    -1,    -1,   103,    62,    21,    64,
      -1,    66,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      75,    34,    35,    -1,    -1,    -1,    39,    -1,    41,    -1,
      -1,    44,     3,     4,     5,     6,     7,    92,    -1,    94,
      95,    12,    -1,    98,    99,   100,    -1,    -1,   103,    62,
      21,    64,    -1,    66,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    75,    34,    35,    -1,    -1,    -1,    39,    -1,
      41,    -1,    -1,    44,    -1,    -1,    -1,    -1,    -1,    92,
      -1,    94,    95,    -1,    -1,    98,    99,   100,    -1,    -1,
     103,    62,    -1,    64,     3,    66,    -1,    -1,    -1,    -1,
      -1,    -1,    11,    -1,    75,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    92,    -1,    94,    95,    -1,    35,    98,    99,   100,
      -1,    -1,   103,    -1,    -1,    -1,    -1,    -1,    47,    -1,
      -1,    -1,    -1,    -1,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    -1,    64,    -1,    66,    -1,    -1,
      -1,    -1,    -1,    -1,    73,    -1,    75,    -1,    62,    -1,
      -1,    -1,    66,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    90,    -1,    -1,    -1,    79,    95,    -1,    -1,    98,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    98,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   106,   107,   108,   109,   110,   111,   112,    62,
      -1,    64,    -1,    66,    -1,    -1,    -1,    -1,    -1,    72,
      -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    -1,    -1,    92,
      93,    94,    95,    96,    97,    98,    99,   100,    62,    -1,
      -1,    -1,    66,    -1,    -1,    69,    -1,   110,    72,    -1,
      -1,    -1,    -1,    -1,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    -1,    -1,    92,    93,
      94,    95,    96,    97,    98,    99,   100,    62,    -1,    -1,
      -1,    66,    -1,    -1,    -1,    -1,   110,    72,    -1,    -1,
      -1,    -1,    -1,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    -1,    -1,    92,    93,    94,
      95,    96,    97,    98,    99,   100,    -1,    62,    -1,    -1,
      -1,    66,    -1,    68,    -1,   110,    71,    72,    -1,    -1,
      -1,    -1,    -1,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    62,    99,   100,    -1,    66,    -1,    68,
      69,   106,   107,    72,    -1,    -1,    -1,    -1,    -1,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    62,
      99,   100,    -1,    66,    -1,    68,    -1,   106,   107,    72,
      -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    62,    99,   100,    -1,    66,
      -1,    68,    -1,   106,   107,    72,    -1,    -1,    -1,    -1,
      -1,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    62,    99,   100,    -1,    66,    -1,    68,    -1,   106,
     107,    72,    -1,    -1,    -1,    -1,    -1,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    -1,    99,   100,
      62,    63,    -1,    -1,    66,   106,   107,    -1,    -1,    -1,
      72,    -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    62,    99,   100,    -1,
      66,    -1,    -1,    69,   106,   107,    72,    -1,    -1,    -1,
      -1,    -1,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    -1,    99,   100,    62,    63,    -1,    -1,    66,
     106,   107,    -1,    -1,    -1,    72,    -1,    -1,    -1,    -1,
      -1,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    -1,    99,   100,    62,    63,    -1,    -1,    66,   106,
     107,    -1,    -1,    -1,    72,    -1,    -1,    -1,    -1,    -1,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      62,    99,   100,    -1,    66,    67,    -1,    -1,   106,   107,
      72,    -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    62,    99,   100,    -1,
      66,    -1,    -1,    69,   106,   107,    72,    -1,    -1,    -1,
      -1,    -1,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    62,    99,   100,    -1,    66,    -1,    68,    -1,
     106,   107,    72,    -1,    -1,    -1,    -1,    -1,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    62,    99,
     100,    -1,    66,    -1,    68,    -1,   106,   107,    72,    -1,
      -1,    -1,    -1,    -1,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    62,    99,   100,    -1,    66,    -1,
      -1,    69,   106,   107,    72,    -1,    -1,    -1,    -1,    -1,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      62,    99,   100,    -1,    66,    67,    -1,    -1,   106,   107,
      72,    -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    -1,    99,   100,    62,
      63,    -1,    -1,    66,   106,   107,    -1,    -1,    -1,    72,
      -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    62,    99,   100,    -1,    66,
      67,    -1,    -1,   106,   107,    72,    -1,    -1,    -1,    -1,
      -1,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    -1,    99,   100,    62,    63,    -1,    -1,    66,   106,
     107,    -1,    -1,    -1,    72,    -1,    -1,    -1,    -1,    -1,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      62,    99,   100,    -1,    66,    -1,    68,    -1,   106,   107,
      72,    -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    62,    99,   100,    -1,
      66,    -1,    68,    -1,   106,   107,    72,    -1,    -1,    -1,
      -1,    -1,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    62,    99,   100,    -1,    66,    -1,    68,    -1,
     106,   107,    72,    -1,    -1,    -1,    -1,    -1,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    62,    99,
     100,    -1,    66,    -1,    68,    -1,   106,   107,    72,    -1,
      -1,    -1,    -1,    -1,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    62,    99,   100,    -1,    66,    67,
      -1,    -1,   106,   107,    72,    -1,    -1,    -1,    -1,    -1,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      62,    99,   100,    -1,    66,    -1,    -1,    69,   106,   107,
      72,    -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    -1,    99,   100,    62,
      -1,    -1,    -1,    66,   106,   107,    -1,    -1,    71,    72,
      -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    62,    99,   100,    -1,    66,
      -1,    -1,    -1,   106,   107,    72,    -1,    -1,    -1,    -1,
      -1,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    66,    99,   100,    -1,    -1,    -1,    72,    -1,   106,
     107,    -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    66,    99,   100,    -1,    -1,    -1,    72,
      -1,   106,   107,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    84,    85,    86,    87,    88,    89,    90,    -1,    92,
      93,    94,    95,    96,    97,    66,    99,   100,    -1,    -1,
      -1,    72,    -1,   106,   107,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    84,    85,    86,    87,    88,    89,    -1,
      -1,    92,    93,    94,    95,    96,    97,    -1,    99,   100
  };

  const unsigned char
  parser::yystos_[] =
  {
       0,     3,     4,     5,     6,     7,     9,    10,    12,    13,
      14,    15,    16,    17,    18,    19,    21,    22,    23,    24,
      25,    26,    27,    31,    33,    34,    35,    36,    39,    40,
      41,    44,    50,    51,    62,    64,    66,    68,    74,    75,
      76,    92,    94,    95,    98,    99,   100,   103,   121,   122,
     123,   126,   127,   128,   129,   131,   132,   137,   152,   153,
     155,   160,   161,   164,   174,   175,   176,   177,   178,   179,
     180,   181,   182,   183,   184,   185,   187,   188,   189,   199,
      52,    64,    70,     3,     3,     3,    62,     3,    12,    68,
     185,    12,   129,   129,     3,     3,     3,     3,    62,    62,
       3,    68,    68,    62,    64,   124,   185,    62,    64,    70,
      62,    70,     3,     6,    64,   139,     3,    35,    66,    98,
     185,    62,    86,     3,     3,     3,    73,   149,   150,   151,
     185,   185,   200,   185,   190,   191,    19,    46,    48,     3,
      66,   185,   185,   185,   185,   185,   185,   124,     0,   123,
      14,    15,    74,   132,   196,    62,    66,    68,    72,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    99,
     100,   106,   107,    86,     3,   195,     3,    86,    86,    86,
      86,   134,     3,   149,    68,   134,    64,   134,    64,   185,
       9,    10,   165,    86,   185,   122,   125,    32,    68,    71,
     190,   195,     3,   190,     3,    68,     3,   138,    37,    68,
      72,    64,    70,    64,     3,    11,    35,    47,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    64,    66,
      73,    75,    90,    95,    98,   166,   167,   168,   169,   171,
     172,   173,   166,    68,   166,   166,   134,   134,    69,     3,
      63,    71,    63,    69,    65,    71,    67,    71,     3,     3,
       3,     3,    51,   133,    46,   129,   190,   185,     3,   185,
     185,   185,   185,   185,   185,   185,   185,   185,   185,   185,
     185,   185,   185,    62,   185,   185,   185,   185,   185,   185,
     185,   166,    69,    65,    71,    62,   166,   170,   166,   166,
       3,   135,   136,    62,    63,    69,   141,     3,   197,   198,
      64,    62,   156,   157,    63,     3,     3,   185,   166,    63,
      65,    62,   185,    63,    65,    62,    64,    63,    62,    65,
      71,     3,     3,   195,    86,   195,    86,    62,    86,   166,
     170,   166,    50,   166,   166,   166,    67,    71,    64,    64,
      63,    87,    79,   141,   166,    69,    86,   151,   185,   185,
     185,    62,    62,    62,    62,    67,    79,     3,    63,    67,
      69,   190,    87,   185,     3,   190,    71,    87,    87,    87,
      69,    71,    87,   149,    86,    86,    64,    79,    65,    71,
     147,     3,     6,    63,    65,   157,   124,    86,    86,    68,
      87,   124,     3,    68,   190,   195,   190,    38,     3,    68,
      65,   170,    65,   170,    63,   170,   170,    63,    65,    67,
      71,    64,   185,   191,   195,    62,    53,    86,    64,   166,
     166,    69,     3,   158,   159,   149,    73,   150,   154,   139,
       6,    62,   185,    63,    64,    69,    63,   166,    62,    64,
      70,    68,    79,    79,   166,   136,    63,   166,   170,   142,
     185,   198,    65,   130,   131,    63,    63,   101,   166,   166,
     185,    30,    28,    37,    63,    65,    63,     6,   139,    87,
      87,   107,   101,    63,    87,   107,   185,   195,    67,    65,
      65,   185,    52,   166,   142,    87,   185,    69,    73,    63,
      71,    63,    71,    63,    63,    67,   149,   162,   185,   190,
     195,     3,   185,   185,    86,    87,    87,    65,   130,   196,
     101,   101,   124,    87,    87,    63,     3,    12,    39,    62,
      92,    94,    95,    98,    99,   100,   186,   188,   124,   166,
      68,    68,    64,   166,   101,   102,    67,    65,    64,    63,
      86,    87,    65,   101,     3,   124,   159,    86,    73,    86,
      67,    63,    65,   130,    63,    65,    62,    68,    68,   166,
     124,   196,     3,    35,    45,    77,   129,   148,   124,   124,
      68,    79,    79,   124,    70,    62,     3,    35,    98,   149,
     186,   186,   186,   186,   186,   186,    62,    66,    72,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     110,   124,    63,   195,   166,   166,   195,   170,    68,    64,
     124,   185,    73,   166,   166,    86,   196,   190,    87,     3,
      12,    20,    45,    77,   143,    86,    62,    62,    66,    79,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    98,   106,   107,   108,   109,   110,
     111,   112,   144,    35,   185,   185,    86,   149,    64,    70,
      64,   166,    63,   190,   185,     3,   186,   186,   186,   186,
     186,   186,   186,   186,    86,   186,    87,   186,   186,   186,
      62,    64,    66,   186,   186,   186,   186,   186,    98,   110,
     124,    65,    65,    87,   185,    87,    87,   166,    12,    20,
      45,   163,    63,    68,   124,    62,    86,     3,    12,   144,
       3,   166,   149,    73,    67,    94,    95,    69,   145,    62,
      68,    68,   170,    63,   195,    86,   195,    64,    86,    63,
      67,    69,   186,   186,   185,   192,   193,   194,   194,   194,
     186,   186,    68,   124,    68,    87,     3,    12,   144,   149,
     166,   134,     3,   145,    62,    87,    63,     3,    79,    86,
      62,   140,    63,    87,    86,    65,   170,    65,   195,   166,
     186,    69,    71,    63,    65,    67,   124,   134,     3,   145,
      63,    87,    62,   134,   140,    63,    68,    68,    63,   135,
     149,    86,    68,   166,    87,    65,    87,   185,   192,    62,
     134,   140,    86,   124,    71,    79,   149,    62,    52,   146,
     124,    87,    63,   166,    87,    64,   101,   149,    62,    86,
     166,   185,    63,   149,    86,    86,    87,   124,   195,   124,
     186,    63,   149,   166,    87,    71,    86,    63,   166,   166,
      68,    65,    86,    63,    87,   124,   166,    86,    87,    87,
     166,    86,   124,    87,   166,    68,   124,    87,   166,   124,
      87,   124,    87,   124,   124
  };

  const unsigned char
  parser::yyr1_[] =
  {
       0,   120,   121,   121,   122,   122,   123,   123,   123,   123,
     123,   123,   123,   123,   123,   123,   123,   123,   123,   123,
     123,   123,   123,   123,   123,   124,   125,   125,   126,   127,
     127,   128,   129,   129,   129,   129,   129,   129,   129,   129,
     129,   129,   130,   130,   131,   131,   132,   132,   132,   133,
     133,   134,   134,   135,   135,   136,   136,   137,   137,   137,
     137,   137,   138,   138,   139,   139,   140,   140,   141,   141,
     142,   142,   143,   143,   143,   143,   143,   143,   143,   143,
     143,   143,   143,   144,   144,   144,   144,   144,   144,   144,
     144,   144,   144,   144,   144,   144,   144,   144,   144,   144,
     144,   144,   144,   144,   144,   144,   144,   144,   144,   144,
     145,   145,   146,   146,   147,   147,   148,   148,   148,   148,
     148,   149,   149,   150,   150,   151,   151,   152,   152,   152,
     152,   153,   154,   154,   154,   154,   155,   155,   156,   156,
     157,   157,   157,   158,   158,   158,   159,   159,   159,   159,
     160,   160,   161,   162,   162,   163,   163,   163,   164,   164,
     165,   165,   165,   166,   166,   166,   167,   167,   167,   168,
     168,   168,   168,   168,   168,   168,   168,   168,   168,   168,
     168,   168,   169,   169,   169,   170,   170,   171,   171,   171,
     172,   172,   173,   173,   173,   173,   173,   173,   173,   173,
     174,   174,   175,   176,   177,   178,   178,   179,   180,   181,
     181,   182,   182,   183,   184,   184,   184,   185,   185,   185,
     185,   185,   185,   185,   185,   185,   185,   185,   185,   185,
     185,   185,   185,   185,   185,   185,   185,   185,   185,   185,
     185,   185,   185,   185,   185,   185,   185,   185,   185,   185,
     185,   185,   185,   185,   185,   185,   185,   185,   185,   185,
     186,   186,   186,   186,   186,   186,   186,   186,   186,   186,
     186,   186,   186,   186,   186,   186,   186,   186,   186,   186,
     186,   186,   186,   186,   186,   186,   186,   186,   186,   186,
     186,   186,   186,   186,   186,   186,   186,   186,   186,   186,
     186,   186,   186,   187,   187,   187,   188,   188,   188,   188,
     188,   188,   188,   188,   188,   188,   188,   188,   188,   189,
     189,   189,   189,   189,   190,   190,   191,   191,   192,   192,
     193,   193,   194,   194,   194,   195,   195,   195,   196,   196,
     196,   197,   197,   197,   198,   198,   199,   200,   200
  };

  const signed char
  parser::yyr2_[] =
  {
       0,     2,     1,     0,     2,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     3,     1,     0,     3,     2,
       2,     1,    10,    10,     7,     6,     5,     7,     8,     8,
       6,     1,     1,     0,     2,     1,     6,     4,     7,     1,
       1,     3,     0,     3,     1,     1,     3,     3,     3,     5,
       7,     7,     3,     1,     1,     3,     3,     0,     4,     0,
       4,     0,     5,     4,     7,     6,    10,    11,     9,     9,
       5,     8,     5,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     3,     2,     2,     4,
       4,     0,     4,     0,     4,     0,     5,     1,     8,     5,
       5,     1,     0,     3,     1,     3,     4,     4,     6,     6,
       4,    10,     3,     1,     1,     0,     7,     5,     2,     1,
       5,     5,     4,     3,     1,     0,     1,     2,     3,     4,
       8,    10,     8,     4,     0,    10,    11,     8,    10,    11,
       8,     8,     6,     1,     4,     1,     1,     1,     1,     1,
       1,     4,     4,     3,     1,     1,     1,     4,     4,     1,
       3,     2,     6,     6,     5,     3,     1,     2,     2,     2,
       3,     5,     1,     1,     1,     1,     1,     1,     1,     1,
       5,     7,     5,     8,     8,     2,     2,     3,     9,     3,
       5,     3,     2,     2,     8,     8,     8,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     2,     2,     2,
       2,     2,     2,     4,     3,     2,     2,     4,     5,     5,
       1,     1,     4,     8,     5,     9,     6,     7,     9,     5,
       3,     3,     3,     3,     3,     4,     4,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     4,     4,
       2,     2,     2,     2,     2,     2,     4,     3,     2,     2,
       4,     5,     5,     5,     5,     1,     8,     8,     8,     5,
       9,     6,     5,     6,    10,     6,     1,     1,     3,     1,
       2,     3,     7,     4,     8,     4,     1,     2,     1,     1,
       1,     1,     1,     1,     1,     0,     3,     1,     1,     3,
       3,     1,     1,     2,     0,     5,     3,     0,     1,     1,
       0,     3,     1,     0,     1,     3,     3,     5,     3
  };




#if YYDEBUG
  const short
  parser::yyrline_[] =
  {
       0,   205,   205,   210,   218,   222,   230,   231,   232,   233,
     234,   235,   236,   237,   238,   239,   240,   241,   242,   243,
     244,   245,   246,   247,   248,   252,   259,   260,   266,   292,
     302,   315,   319,   323,   327,   333,   338,   341,   352,   355,
     358,   361,   367,   368,   372,   373,   377,   381,   385,   391,
     391,   396,   397,   401,   402,   406,   410,   420,   431,   437,
     443,   452,   458,   459,   463,   464,   468,   469,   475,   476,
     480,   503,   512,   516,   520,   526,   533,   539,   547,   554,
     562,   566,   571,   578,   579,   580,   581,   582,   583,   584,
     585,   586,   587,   588,   589,   590,   591,   592,   593,   594,
     595,   596,   597,   598,   599,   600,   601,   602,   603,   604,
     608,   609,   613,   614,   620,   643,   655,   660,   663,   671,
     677,   686,   687,   691,   692,   696,   700,   710,   715,   720,
     725,   734,   741,   742,   743,   744,   750,   754,   761,   762,
     766,   769,   772,   778,   779,   780,   785,   789,   793,   797,
     806,   813,   828,   837,   849,   856,   862,   870,   882,   887,
     898,   902,   906,   917,   918,   922,   926,   927,   928,   932,
     933,   934,   938,   943,   948,   949,   950,   951,   955,   959,
     960,   961,   965,   969,   974,   982,   983,   987,   991,   995,
    1005,  1009,  1016,  1017,  1018,  1019,  1020,  1021,  1022,  1023,
    1029,  1033,  1040,  1047,  1055,  1062,  1063,  1067,  1074,  1081,
    1085,  1092,  1093,  1099,  1104,  1109,  1114,  1124,  1125,  1126,
    1127,  1128,  1129,  1130,  1131,  1132,  1133,  1134,  1135,  1136,
    1137,  1138,  1139,  1140,  1141,  1142,  1143,  1146,  1147,  1148,
    1149,  1150,  1151,  1154,  1164,  1168,  1169,  1170,  1174,  1179,
    1191,  1193,  1196,  1202,  1207,  1213,  1220,  1226,  1231,  1237,
    1250,  1251,  1252,  1253,  1254,  1255,  1256,  1257,  1258,  1259,
    1260,  1261,  1262,  1263,  1264,  1265,  1266,  1267,  1268,  1269,
    1272,  1273,  1274,  1275,  1276,  1277,  1280,  1290,  1294,  1295,
    1296,  1300,  1305,  1315,  1325,  1337,  1340,  1344,  1348,  1354,
    1360,  1367,  1373,  1382,  1388,  1395,  1403,  1404,  1405,  1406,
    1409,  1411,  1412,  1413,  1416,  1424,  1432,  1435,  1441,  1445,
    1446,  1447,  1448,  1449,  1453,  1454,  1458,  1459,  1463,  1468,
    1477,  1481,  1485,  1486,  1487,  1491,  1494,  1498,  1504,  1505,
    1506,  1510,  1511,  1512,  1516,  1519,  1525,  1532,  1533
  };

  void
  parser::yy_stack_print_ () const
  {
    *yycdebug_ << "Stack now";
    for (stack_type::const_iterator
           i = yystack_.begin (),
           i_end = yystack_.end ();
         i != i_end; ++i)
      *yycdebug_ << ' ' << int (i->state);
    *yycdebug_ << '\n';
  }

  void
  parser::yy_reduce_print_ (int yyrule) const
  {
    int yylno = yyrline_[yyrule];
    int yynrhs = yyr2_[yyrule];
    // Print the symbols being reduced, and their result.
    *yycdebug_ << "Reducing stack by rule " << yyrule - 1
               << " (line " << yylno << "):\n";
    // The symbols being reduced.
    for (int yyi = 0; yyi < yynrhs; yyi++)
      YY_SYMBOL_PRINT ("   $" << yyi + 1 << " =",
                       yystack_[(yynrhs) - (yyi + 1)]);
  }
#endif // YYDEBUG


#line 6 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
} // fin
#line 5486 "parser.tab.c"

#line 1536 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"


void fin::parser::error(const location_type& l, const std::string& m) {
    diag.reportError(l, m);
}
