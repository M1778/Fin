// A Bison parser, made by GNU Bison 3.8.2.

// Skeleton interface for Bison LALR(1) parsers in C++

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


/**
 ** \file parser.tab.h
 ** Define the fin::parser class.
 */

// C++ LALR(1) parser skeleton written by Akim Demaille.

// DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
// especially those whose name start with YY_ or yy_.  They are
// private implementation details that can be changed or removed.

#ifndef YY_YY_PARSER_TAB_H_INCLUDED
# define YY_YY_PARSER_TAB_H_INCLUDED
// "%code requires" blocks.
#line 11 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"

    #include <string>
    #include <vector>
    #include <memory>
    #include <utility>
    
    namespace fin { class location; }
    #include "ast/ASTNode.hpp"
    namespace fin { class DiagnosticEngine; }

#line 60 "parser.tab.h"

# include <cassert>
# include <cstdlib> // std::abort
# include <iostream>
# include <stdexcept>
# include <string>
# include <vector>

#if defined __cplusplus
# define YY_CPLUSPLUS __cplusplus
#else
# define YY_CPLUSPLUS 199711L
#endif

// Support move semantics when possible.
#if 201103L <= YY_CPLUSPLUS
# define YY_MOVE           std::move
# define YY_MOVE_OR_COPY   move
# define YY_MOVE_REF(Type) Type&&
# define YY_RVREF(Type)    Type&&
# define YY_COPY(Type)     Type
#else
# define YY_MOVE
# define YY_MOVE_OR_COPY   copy
# define YY_MOVE_REF(Type) Type&
# define YY_RVREF(Type)    const Type&
# define YY_COPY(Type)     const Type&
#endif

// Support noexcept when possible.
#if 201103L <= YY_CPLUSPLUS
# define YY_NOEXCEPT noexcept
# define YY_NOTHROW
#else
# define YY_NOEXCEPT
# define YY_NOTHROW throw ()
#endif

// Support constexpr when possible.
#if 201703 <= YY_CPLUSPLUS
# define YY_CONSTEXPR constexpr
#else
# define YY_CONSTEXPR
#endif
# include "location.hh"
#include <typeinfo>
#ifndef YY_ASSERT
# include <cassert>
# define YY_ASSERT assert
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

#line 6 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
namespace fin {
#line 201 "parser.tab.h"




  /// A Bison parser.
  class parser
  {
  public:
#ifdef YYSTYPE
# ifdef __GNUC__
#  pragma GCC message "bison: do not #define YYSTYPE in C++, use %define api.value.type"
# endif
    typedef YYSTYPE value_type;
#else
  /// A buffer to store and retrieve objects.
  ///
  /// Sort of a variant, but does not keep track of the nature
  /// of the stored data, since that knowledge is available
  /// via the current parser state.
  class value_type
  {
  public:
    /// Type of *this.
    typedef value_type self_type;

    /// Empty construction.
    value_type () YY_NOEXCEPT
      : yyraw_ ()
      , yytypeid_ (YY_NULLPTR)
    {}

    /// Construct and fill.
    template <typename T>
    value_type (YY_RVREF (T) t)
      : yytypeid_ (&typeid (T))
    {
      YY_ASSERT (sizeof (T) <= size);
      new (yyas_<T> ()) T (YY_MOVE (t));
    }

#if 201103L <= YY_CPLUSPLUS
    /// Non copyable.
    value_type (const self_type&) = delete;
    /// Non copyable.
    self_type& operator= (const self_type&) = delete;
#endif

    /// Destruction, allowed only if empty.
    ~value_type () YY_NOEXCEPT
    {
      YY_ASSERT (!yytypeid_);
    }

# if 201103L <= YY_CPLUSPLUS
    /// Instantiate a \a T in here from \a t.
    template <typename T, typename... U>
    T&
    emplace (U&&... u)
    {
      YY_ASSERT (!yytypeid_);
      YY_ASSERT (sizeof (T) <= size);
      yytypeid_ = & typeid (T);
      return *new (yyas_<T> ()) T (std::forward <U>(u)...);
    }
# else
    /// Instantiate an empty \a T in here.
    template <typename T>
    T&
    emplace ()
    {
      YY_ASSERT (!yytypeid_);
      YY_ASSERT (sizeof (T) <= size);
      yytypeid_ = & typeid (T);
      return *new (yyas_<T> ()) T ();
    }

    /// Instantiate a \a T in here from \a t.
    template <typename T>
    T&
    emplace (const T& t)
    {
      YY_ASSERT (!yytypeid_);
      YY_ASSERT (sizeof (T) <= size);
      yytypeid_ = & typeid (T);
      return *new (yyas_<T> ()) T (t);
    }
# endif

    /// Instantiate an empty \a T in here.
    /// Obsolete, use emplace.
    template <typename T>
    T&
    build ()
    {
      return emplace<T> ();
    }

    /// Instantiate a \a T in here from \a t.
    /// Obsolete, use emplace.
    template <typename T>
    T&
    build (const T& t)
    {
      return emplace<T> (t);
    }

    /// Accessor to a built \a T.
    template <typename T>
    T&
    as () YY_NOEXCEPT
    {
      YY_ASSERT (yytypeid_);
      YY_ASSERT (*yytypeid_ == typeid (T));
      YY_ASSERT (sizeof (T) <= size);
      return *yyas_<T> ();
    }

    /// Const accessor to a built \a T (for %printer).
    template <typename T>
    const T&
    as () const YY_NOEXCEPT
    {
      YY_ASSERT (yytypeid_);
      YY_ASSERT (*yytypeid_ == typeid (T));
      YY_ASSERT (sizeof (T) <= size);
      return *yyas_<T> ();
    }

    /// Swap the content with \a that, of same type.
    ///
    /// Both variants must be built beforehand, because swapping the actual
    /// data requires reading it (with as()), and this is not possible on
    /// unconstructed variants: it would require some dynamic testing, which
    /// should not be the variant's responsibility.
    /// Swapping between built and (possibly) non-built is done with
    /// self_type::move ().
    template <typename T>
    void
    swap (self_type& that) YY_NOEXCEPT
    {
      YY_ASSERT (yytypeid_);
      YY_ASSERT (*yytypeid_ == *that.yytypeid_);
      std::swap (as<T> (), that.as<T> ());
    }

    /// Move the content of \a that to this.
    ///
    /// Destroys \a that.
    template <typename T>
    void
    move (self_type& that)
    {
# if 201103L <= YY_CPLUSPLUS
      emplace<T> (std::move (that.as<T> ()));
# else
      emplace<T> ();
      swap<T> (that);
# endif
      that.destroy<T> ();
    }

# if 201103L <= YY_CPLUSPLUS
    /// Move the content of \a that to this.
    template <typename T>
    void
    move (self_type&& that)
    {
      emplace<T> (std::move (that.as<T> ()));
      that.destroy<T> ();
    }
#endif

    /// Copy the content of \a that to this.
    template <typename T>
    void
    copy (const self_type& that)
    {
      emplace<T> (that.as<T> ());
    }

    /// Destroy the stored \a T.
    template <typename T>
    void
    destroy ()
    {
      as<T> ().~T ();
      yytypeid_ = YY_NULLPTR;
    }

  private:
#if YY_CPLUSPLUS < 201103L
    /// Non copyable.
    value_type (const self_type&);
    /// Non copyable.
    self_type& operator= (const self_type&);
#endif

    /// Accessor to raw memory as \a T.
    template <typename T>
    T*
    yyas_ () YY_NOEXCEPT
    {
      void *yyp = yyraw_;
      return static_cast<T*> (yyp);
     }

    /// Const accessor to raw memory as \a T.
    template <typename T>
    const T*
    yyas_ () const YY_NOEXCEPT
    {
      const void *yyp = yyraw_;
      return static_cast<const T*> (yyp);
     }

    /// An auxiliary type to compute the largest semantic type.
    union union_type
    {
      // visibility_opt
      char dummy1[sizeof (bool)];

      // operator_symbol
      char dummy2[sizeof (fin::ASTTokenKind)];

      // macro_param
      char dummy3[sizeof (fin::MacroParam)];

      // macro_rule
      char dummy4[sizeof (fin::MacroRule)];

      // enum_value
      char dummy5[sizeof (std::pair<std::string, std::unique_ptr<fin::Expression>>)];

      // extern_params
      char dummy6[sizeof (std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool>)];

      // IDENTIFIER
      // INTEGER
      // FLOAT
      // STRING_LITERAL
      // CHAR_LITERAL
      // TYPE_ID
      // attr_id
      // dotted_path
      // primitive_type
      char dummy7[sizeof (std::string)];

      // struct_item_rest
      // interface_item_rest
      // implements_item_rest
      char dummy8[sizeof (std::unique_ptr<fin::ASTNode>)];

      // attribute
      char dummy9[sizeof (std::unique_ptr<fin::Attribute>)];

      // block
      char dummy10[sizeof (std::unique_ptr<fin::Block>)];

      // super_expression
      // lambda_expression
      // expression
      // no_struct_expression
      // static_method_call
      // primary_no_struct
      // literal
      // prototype_literal
      char dummy11[sizeof (std::unique_ptr<fin::Expression>)];

      // generic_param
      char dummy12[sizeof (std::unique_ptr<fin::GenericParam>)];

      // implements_body_content
      char dummy13[sizeof (std::unique_ptr<fin::ImplementsBlock>)];

      // interface_body_content
      char dummy14[sizeof (std::unique_ptr<fin::InterfaceDeclaration>)];

      // param
      char dummy15[sizeof (std::unique_ptr<fin::Parameter>)];

      // program
      char dummy16[sizeof (std::unique_ptr<fin::Program>)];

      // statement
      // annotated_declaration
      // declaration_with_vis
      // bare_declaration
      // declaration_body
      // import_statement
      // define_declaration
      // macro_declaration
      // type_definition
      // implements_block
      // special_declaration
      // variable_declaration
      // if_statement
      // while_loop
      // for_loop
      // foreach_loop
      // control_statement
      // delete_statement
      // try_catch_statement
      // blame_statement
      // return_statement
      // expression_statement
      char dummy17[sizeof (std::unique_ptr<fin::Statement>)];

      // struct_body_content
      char dummy18[sizeof (std::unique_ptr<fin::StructDeclaration>)];

      // implements_opt
      // type
      // type_no_annot
      // base_type
      // fn_type
      // pointer_type
      // array_type
      char dummy19[sizeof (std::unique_ptr<fin::TypeNode>)];

      // macro_param_list
      char dummy20[sizeof (std::vector<fin::MacroParam>)];

      // macro_rules
      char dummy21[sizeof (std::vector<fin::MacroRule>)];

      // field_assignments
      // enum_values
      char dummy22[sizeof (std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>>)];

      // prototype_elements
      char dummy23[sizeof (std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>>)];

      // import_list
      char dummy24[sizeof (std::vector<std::string>)];

      // attributes_opt
      // attribute_list
      char dummy25[sizeof (std::vector<std::unique_ptr<fin::Attribute>>)];

      // arguments
      // expression_list
      // macro_arg_item
      // macro_arg_list_body
      // macro_arguments
      char dummy26[sizeof (std::vector<std::unique_ptr<fin::Expression>>)];

      // generic_params_opt
      // generic_param_list
      // operator_generics_opt
      char dummy27[sizeof (std::vector<std::unique_ptr<fin::GenericParam>>)];

      // operator_params_opt
      // params
      // param_list
      char dummy28[sizeof (std::vector<std::unique_ptr<fin::Parameter>>)];

      // statements
      // block_stmts
      char dummy29[sizeof (std::vector<std::unique_ptr<fin::Statement>>)];

      // inheritance_opt
      // type_list
      char dummy30[sizeof (std::vector<std::unique_ptr<fin::TypeNode>>)];
    };

    /// The size of the largest semantic type.
    enum { size = sizeof (union_type) };

    /// A buffer to store semantic values.
    union
    {
      /// Strongest alignment constraints.
      long double yyalign_me_;
      /// A buffer large enough to store any of the semantic values.
      char yyraw_[size];
    };

    /// Whether the content is built: if defined, the name of the stored type.
    const std::type_info *yytypeid_;
  };

#endif
    /// Backward compatibility (Bison 3.8).
    typedef value_type semantic_type;

    /// Symbol locations.
    typedef location location_type;

    /// Syntax errors thrown from user actions.
    struct syntax_error : std::runtime_error
    {
      syntax_error (const location_type& l, const std::string& m)
        : std::runtime_error (m)
        , location (l)
      {}

      syntax_error (const syntax_error& s)
        : std::runtime_error (s.what ())
        , location (s.location)
      {}

      ~syntax_error () YY_NOEXCEPT YY_NOTHROW;

      location_type location;
    };

    /// Token kinds.
    struct token
    {
      enum token_kind_type
      {
        YYEMPTY = -2,
    END = 0,                       // "end of file"
    YYerror = 256,                 // error
    YYUNDEF = 257,                 // "invalid token"
    IDENTIFIER = 258,              // IDENTIFIER
    INTEGER = 259,                 // INTEGER
    FLOAT = 260,                   // FLOAT
    STRING_LITERAL = 261,          // STRING_LITERAL
    CHAR_LITERAL = 262,            // CHAR_LITERAL
    TYPE_ID = 263,                 // TYPE_ID
    KW_LET = 264,                  // KW_LET
    KW_CONST = 265,                // KW_CONST
    KW_AUTO = 266,                 // KW_AUTO
    KW_FUN = 267,                  // KW_FUN
    KW_RETURN = 268,               // KW_RETURN
    KW_PUB = 269,                  // KW_PUB
    KW_PRIV = 270,                 // KW_PRIV
    KW_STRUCT = 271,               // KW_STRUCT
    KW_ENUM = 272,                 // KW_ENUM
    KW_INTERFACE = 273,            // KW_INTERFACE
    KW_MACRO = 274,                // KW_MACRO
    KW_STATIC = 275,               // KW_STATIC
    KW_NULL = 276,                 // KW_NULL
    KW_WHILE = 277,                // KW_WHILE
    KW_FOR = 278,                  // KW_FOR
    KW_FOREACH = 279,              // KW_FOREACH
    KW_BREAK = 280,                // KW_BREAK
    KW_CONTINUE = 281,             // KW_CONTINUE
    KW_IF = 282,                   // KW_IF
    KW_ELSE = 283,                 // KW_ELSE
    KW_ELSEIF = 284,               // KW_ELSEIF
    KW_IN = 285,                   // KW_IN
    KW_TRY = 286,                  // KW_TRY
    KW_CATCH = 287,                // KW_CATCH
    KW_BLAME = 288,                // KW_BLAME
    KW_SUPER = 289,                // KW_SUPER
    KW_SELF_TYPE = 290,            // KW_SELF_TYPE
    KW_IMPORT = 291,               // KW_IMPORT
    KW_AS = 292,                   // KW_AS
    KW_FROM = 293,                 // KW_FROM
    KW_NEW = 294,                  // KW_NEW
    KW_DELETE = 295,               // KW_DELETE
    KW_SIZEOF = 296,               // KW_SIZEOF
    KW_TYPEOF = 297,               // KW_TYPEOF
    KW_AS_PTR = 298,               // KW_AS_PTR
    KW_CAST = 299,                 // KW_CAST
    KW_OPERATOR = 300,             // KW_OPERATOR
    KW_SPECIAL = 301,              // KW_SPECIAL
    KW_FN_TYPE = 302,              // KW_FN_TYPE
    KW_DEFINE = 303,               // KW_DEFINE
    KW_M1778 = 304,                // KW_M1778
    KW_TYPE = 305,                 // KW_TYPE
    KW_CLASS = 306,                // KW_CLASS
    KW_IMPLEMENTS = 307,           // KW_IMPLEMENTS
    KW_ANY = 308,                  // KW_ANY
    TYPE_INT = 309,                // TYPE_INT
    TYPE_FLOAT = 310,              // TYPE_FLOAT
    TYPE_DOUBLE = 311,             // TYPE_DOUBLE
    TYPE_BOOL = 312,               // TYPE_BOOL
    TYPE_STRING = 313,             // TYPE_STRING
    TYPE_CHAR = 314,               // TYPE_CHAR
    TYPE_VOID = 315,               // TYPE_VOID
    TYPE_LONG = 316,               // TYPE_LONG
    LPAREN = 317,                  // LPAREN
    RPAREN = 318,                  // RPAREN
    LBRACE = 319,                  // LBRACE
    RBRACE = 320,                  // RBRACE
    LBRACKET = 321,                // LBRACKET
    RBRACKET = 322,                // RBRACKET
    SEMICOLON = 323,               // SEMICOLON
    COLON = 324,                   // COLON
    DOUBLE_COLON = 325,            // DOUBLE_COLON
    COMMA = 326,                   // COMMA
    DOT = 327,                     // DOT
    ELLIPSIS = 328,                // ELLIPSIS
    AT = 329,                      // AT
    DOLLAR = 330,                  // DOLLAR
    HASH = 331,                    // HASH
    TILDE = 332,                   // TILDE
    QUESTION = 333,                // QUESTION
    EQUAL = 334,                   // EQUAL
    PLUSEQUAL = 335,               // PLUSEQUAL
    MINUSEQUAL = 336,              // MINUSEQUAL
    MULTEQUAL = 337,               // MULTEQUAL
    DIVEQUAL = 338,                // DIVEQUAL
    EQEQ = 339,                    // EQEQ
    NOTEQ = 340,                   // NOTEQ
    LT = 341,                      // LT
    GT = 342,                      // GT
    LTEQ = 343,                    // LTEQ
    GTEQ = 344,                    // GTEQ
    AND = 345,                     // AND
    OR = 346,                      // OR
    NOT = 347,                     // NOT
    PLUS = 348,                    // PLUS
    MINUS = 349,                   // MINUS
    MULT = 350,                    // MULT
    DIV = 351,                     // DIV
    MOD = 352,                     // MOD
    AMPERSAND = 353,               // AMPERSAND
    INCREMENT = 354,               // INCREMENT
    DECREMENT = 355,               // DECREMENT
    ARROW = 356,                   // ARROW
    RARROW = 357,                  // RARROW
    KW_QUOTE = 358,                // KW_QUOTE
    HASH_FOR = 359,                // HASH_FOR
    HASH_INDEX = 360,              // HASH_INDEX
    SHIFTLEFT = 361,               // SHIFTLEFT
    SHIFTRIGHT = 362,              // SHIFTRIGHT
    SHIFTLEFTEQUAL = 363,          // SHIFTLEFTEQUAL
    SHIFTRIGHTEQUAL = 364,         // SHIFTRIGHTEQUAL
    PIPE = 365,                    // PIPE
    CARET = 366,                   // CARET
    BACKTICK = 367,                // BACKTICK
    TYPE_ANNOT_PREC = 368,         // TYPE_ANNOT_PREC
    UMINUS = 369,                  // UMINUS
    ADDRESSOF_PREC = 370,          // ADDRESSOF_PREC
    DEREFERENCE_PREC = 371,        // DEREFERENCE_PREC
    HIGH_PREC = 372,               // HIGH_PREC
    TYPE_PREC = 373,               // TYPE_PREC
    KW_IFX = 374                   // KW_IFX
      };
      /// Backward compatibility alias (Bison 3.6).
      typedef token_kind_type yytokentype;
    };

    /// Token kind, as returned by yylex.
    typedef token::token_kind_type token_kind_type;

    /// Backward compatibility alias (Bison 3.6).
    typedef token_kind_type token_type;

    /// Symbol kinds.
    struct symbol_kind
    {
      enum symbol_kind_type
      {
        YYNTOKENS = 120, ///< Number of tokens.
        S_YYEMPTY = -2,
        S_YYEOF = 0,                             // "end of file"
        S_YYerror = 1,                           // error
        S_YYUNDEF = 2,                           // "invalid token"
        S_IDENTIFIER = 3,                        // IDENTIFIER
        S_INTEGER = 4,                           // INTEGER
        S_FLOAT = 5,                             // FLOAT
        S_STRING_LITERAL = 6,                    // STRING_LITERAL
        S_CHAR_LITERAL = 7,                      // CHAR_LITERAL
        S_TYPE_ID = 8,                           // TYPE_ID
        S_KW_LET = 9,                            // KW_LET
        S_KW_CONST = 10,                         // KW_CONST
        S_KW_AUTO = 11,                          // KW_AUTO
        S_KW_FUN = 12,                           // KW_FUN
        S_KW_RETURN = 13,                        // KW_RETURN
        S_KW_PUB = 14,                           // KW_PUB
        S_KW_PRIV = 15,                          // KW_PRIV
        S_KW_STRUCT = 16,                        // KW_STRUCT
        S_KW_ENUM = 17,                          // KW_ENUM
        S_KW_INTERFACE = 18,                     // KW_INTERFACE
        S_KW_MACRO = 19,                         // KW_MACRO
        S_KW_STATIC = 20,                        // KW_STATIC
        S_KW_NULL = 21,                          // KW_NULL
        S_KW_WHILE = 22,                         // KW_WHILE
        S_KW_FOR = 23,                           // KW_FOR
        S_KW_FOREACH = 24,                       // KW_FOREACH
        S_KW_BREAK = 25,                         // KW_BREAK
        S_KW_CONTINUE = 26,                      // KW_CONTINUE
        S_KW_IF = 27,                            // KW_IF
        S_KW_ELSE = 28,                          // KW_ELSE
        S_KW_ELSEIF = 29,                        // KW_ELSEIF
        S_KW_IN = 30,                            // KW_IN
        S_KW_TRY = 31,                           // KW_TRY
        S_KW_CATCH = 32,                         // KW_CATCH
        S_KW_BLAME = 33,                         // KW_BLAME
        S_KW_SUPER = 34,                         // KW_SUPER
        S_KW_SELF_TYPE = 35,                     // KW_SELF_TYPE
        S_KW_IMPORT = 36,                        // KW_IMPORT
        S_KW_AS = 37,                            // KW_AS
        S_KW_FROM = 38,                          // KW_FROM
        S_KW_NEW = 39,                           // KW_NEW
        S_KW_DELETE = 40,                        // KW_DELETE
        S_KW_SIZEOF = 41,                        // KW_SIZEOF
        S_KW_TYPEOF = 42,                        // KW_TYPEOF
        S_KW_AS_PTR = 43,                        // KW_AS_PTR
        S_KW_CAST = 44,                          // KW_CAST
        S_KW_OPERATOR = 45,                      // KW_OPERATOR
        S_KW_SPECIAL = 46,                       // KW_SPECIAL
        S_KW_FN_TYPE = 47,                       // KW_FN_TYPE
        S_KW_DEFINE = 48,                        // KW_DEFINE
        S_KW_M1778 = 49,                         // KW_M1778
        S_KW_TYPE = 50,                          // KW_TYPE
        S_KW_CLASS = 51,                         // KW_CLASS
        S_KW_IMPLEMENTS = 52,                    // KW_IMPLEMENTS
        S_KW_ANY = 53,                           // KW_ANY
        S_TYPE_INT = 54,                         // TYPE_INT
        S_TYPE_FLOAT = 55,                       // TYPE_FLOAT
        S_TYPE_DOUBLE = 56,                      // TYPE_DOUBLE
        S_TYPE_BOOL = 57,                        // TYPE_BOOL
        S_TYPE_STRING = 58,                      // TYPE_STRING
        S_TYPE_CHAR = 59,                        // TYPE_CHAR
        S_TYPE_VOID = 60,                        // TYPE_VOID
        S_TYPE_LONG = 61,                        // TYPE_LONG
        S_LPAREN = 62,                           // LPAREN
        S_RPAREN = 63,                           // RPAREN
        S_LBRACE = 64,                           // LBRACE
        S_RBRACE = 65,                           // RBRACE
        S_LBRACKET = 66,                         // LBRACKET
        S_RBRACKET = 67,                         // RBRACKET
        S_SEMICOLON = 68,                        // SEMICOLON
        S_COLON = 69,                            // COLON
        S_DOUBLE_COLON = 70,                     // DOUBLE_COLON
        S_COMMA = 71,                            // COMMA
        S_DOT = 72,                              // DOT
        S_ELLIPSIS = 73,                         // ELLIPSIS
        S_AT = 74,                               // AT
        S_DOLLAR = 75,                           // DOLLAR
        S_HASH = 76,                             // HASH
        S_TILDE = 77,                            // TILDE
        S_QUESTION = 78,                         // QUESTION
        S_EQUAL = 79,                            // EQUAL
        S_PLUSEQUAL = 80,                        // PLUSEQUAL
        S_MINUSEQUAL = 81,                       // MINUSEQUAL
        S_MULTEQUAL = 82,                        // MULTEQUAL
        S_DIVEQUAL = 83,                         // DIVEQUAL
        S_EQEQ = 84,                             // EQEQ
        S_NOTEQ = 85,                            // NOTEQ
        S_LT = 86,                               // LT
        S_GT = 87,                               // GT
        S_LTEQ = 88,                             // LTEQ
        S_GTEQ = 89,                             // GTEQ
        S_AND = 90,                              // AND
        S_OR = 91,                               // OR
        S_NOT = 92,                              // NOT
        S_PLUS = 93,                             // PLUS
        S_MINUS = 94,                            // MINUS
        S_MULT = 95,                             // MULT
        S_DIV = 96,                              // DIV
        S_MOD = 97,                              // MOD
        S_AMPERSAND = 98,                        // AMPERSAND
        S_INCREMENT = 99,                        // INCREMENT
        S_DECREMENT = 100,                       // DECREMENT
        S_ARROW = 101,                           // ARROW
        S_RARROW = 102,                          // RARROW
        S_KW_QUOTE = 103,                        // KW_QUOTE
        S_HASH_FOR = 104,                        // HASH_FOR
        S_HASH_INDEX = 105,                      // HASH_INDEX
        S_SHIFTLEFT = 106,                       // SHIFTLEFT
        S_SHIFTRIGHT = 107,                      // SHIFTRIGHT
        S_SHIFTLEFTEQUAL = 108,                  // SHIFTLEFTEQUAL
        S_SHIFTRIGHTEQUAL = 109,                 // SHIFTRIGHTEQUAL
        S_PIPE = 110,                            // PIPE
        S_CARET = 111,                           // CARET
        S_BACKTICK = 112,                        // BACKTICK
        S_TYPE_ANNOT_PREC = 113,                 // TYPE_ANNOT_PREC
        S_UMINUS = 114,                          // UMINUS
        S_ADDRESSOF_PREC = 115,                  // ADDRESSOF_PREC
        S_DEREFERENCE_PREC = 116,                // DEREFERENCE_PREC
        S_HIGH_PREC = 117,                       // HIGH_PREC
        S_TYPE_PREC = 118,                       // TYPE_PREC
        S_KW_IFX = 119,                          // KW_IFX
        S_YYACCEPT = 120,                        // $accept
        S_program = 121,                         // program
        S_statements = 122,                      // statements
        S_statement = 123,                       // statement
        S_block = 124,                           // block
        S_block_stmts = 125,                     // block_stmts
        S_annotated_declaration = 126,           // annotated_declaration
        S_declaration_with_vis = 127,            // declaration_with_vis
        S_bare_declaration = 128,                // bare_declaration
        S_declaration_body = 129,                // declaration_body
        S_attributes_opt = 130,                  // attributes_opt
        S_attribute_list = 131,                  // attribute_list
        S_attribute = 132,                       // attribute
        S_attr_id = 133,                         // attr_id
        S_generic_params_opt = 134,              // generic_params_opt
        S_generic_param_list = 135,              // generic_param_list
        S_generic_param = 136,                   // generic_param
        S_import_statement = 137,                // import_statement
        S_import_list = 138,                     // import_list
        S_dotted_path = 139,                     // dotted_path
        S_operator_params_opt = 140,             // operator_params_opt
        S_inheritance_opt = 141,                 // inheritance_opt
        S_struct_body_content = 142,             // struct_body_content
        S_struct_item_rest = 143,                // struct_item_rest
        S_operator_symbol = 144,                 // operator_symbol
        S_operator_generics_opt = 145,           // operator_generics_opt
        S_implements_opt = 146,                  // implements_opt
        S_interface_body_content = 147,          // interface_body_content
        S_interface_item_rest = 148,             // interface_item_rest
        S_params = 149,                          // params
        S_param_list = 150,                      // param_list
        S_param = 151,                           // param
        S_super_expression = 152,                // super_expression
        S_define_declaration = 153,              // define_declaration
        S_extern_params = 154,                   // extern_params
        S_macro_declaration = 155,               // macro_declaration
        S_macro_rules = 156,                     // macro_rules
        S_macro_rule = 157,                      // macro_rule
        S_macro_param_list = 158,                // macro_param_list
        S_macro_param = 159,                     // macro_param
        S_type_definition = 160,                 // type_definition
        S_implements_block = 161,                // implements_block
        S_implements_body_content = 162,         // implements_body_content
        S_implements_item_rest = 163,            // implements_item_rest
        S_special_declaration = 164,             // special_declaration
        S_variable_declaration = 165,            // variable_declaration
        S_type = 166,                            // type
        S_type_no_annot = 167,                   // type_no_annot
        S_base_type = 168,                       // base_type
        S_fn_type = 169,                         // fn_type
        S_type_list = 170,                       // type_list
        S_pointer_type = 171,                    // pointer_type
        S_array_type = 172,                      // array_type
        S_primitive_type = 173,                  // primitive_type
        S_if_statement = 174,                    // if_statement
        S_while_loop = 175,                      // while_loop
        S_for_loop = 176,                        // for_loop
        S_foreach_loop = 177,                    // foreach_loop
        S_control_statement = 178,               // control_statement
        S_delete_statement = 179,                // delete_statement
        S_try_catch_statement = 180,             // try_catch_statement
        S_blame_statement = 181,                 // blame_statement
        S_return_statement = 182,                // return_statement
        S_expression_statement = 183,            // expression_statement
        S_lambda_expression = 184,               // lambda_expression
        S_expression = 185,                      // expression
        S_no_struct_expression = 186,            // no_struct_expression
        S_static_method_call = 187,              // static_method_call
        S_primary_no_struct = 188,               // primary_no_struct
        S_literal = 189,                         // literal
        S_arguments = 190,                       // arguments
        S_expression_list = 191,                 // expression_list
        S_macro_arg_item = 192,                  // macro_arg_item
        S_macro_arg_list_body = 193,             // macro_arg_list_body
        S_macro_arguments = 194,                 // macro_arguments
        S_field_assignments = 195,               // field_assignments
        S_visibility_opt = 196,                  // visibility_opt
        S_enum_values = 197,                     // enum_values
        S_enum_value = 198,                      // enum_value
        S_prototype_literal = 199,               // prototype_literal
        S_prototype_elements = 200               // prototype_elements
      };
    };

    /// (Internal) symbol kind.
    typedef symbol_kind::symbol_kind_type symbol_kind_type;

    /// The number of tokens.
    static const symbol_kind_type YYNTOKENS = symbol_kind::YYNTOKENS;

    /// A complete symbol.
    ///
    /// Expects its Base type to provide access to the symbol kind
    /// via kind ().
    ///
    /// Provide access to semantic value and location.
    template <typename Base>
    struct basic_symbol : Base
    {
      /// Alias to Base.
      typedef Base super_type;

      /// Default constructor.
      basic_symbol () YY_NOEXCEPT
        : value ()
        , location ()
      {}

#if 201103L <= YY_CPLUSPLUS
      /// Move constructor.
      basic_symbol (basic_symbol&& that)
        : Base (std::move (that))
        , value ()
        , location (std::move (that.location))
      {
        switch (this->kind ())
    {
      case symbol_kind::S_visibility_opt: // visibility_opt
        value.move< bool > (std::move (that.value));
        break;

      case symbol_kind::S_operator_symbol: // operator_symbol
        value.move< fin::ASTTokenKind > (std::move (that.value));
        break;

      case symbol_kind::S_macro_param: // macro_param
        value.move< fin::MacroParam > (std::move (that.value));
        break;

      case symbol_kind::S_macro_rule: // macro_rule
        value.move< fin::MacroRule > (std::move (that.value));
        break;

      case symbol_kind::S_enum_value: // enum_value
        value.move< std::pair<std::string, std::unique_ptr<fin::Expression>> > (std::move (that.value));
        break;

      case symbol_kind::S_extern_params: // extern_params
        value.move< std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > (std::move (that.value));
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
        value.move< std::string > (std::move (that.value));
        break;

      case symbol_kind::S_struct_item_rest: // struct_item_rest
      case symbol_kind::S_interface_item_rest: // interface_item_rest
      case symbol_kind::S_implements_item_rest: // implements_item_rest
        value.move< std::unique_ptr<fin::ASTNode> > (std::move (that.value));
        break;

      case symbol_kind::S_attribute: // attribute
        value.move< std::unique_ptr<fin::Attribute> > (std::move (that.value));
        break;

      case symbol_kind::S_block: // block
        value.move< std::unique_ptr<fin::Block> > (std::move (that.value));
        break;

      case symbol_kind::S_super_expression: // super_expression
      case symbol_kind::S_lambda_expression: // lambda_expression
      case symbol_kind::S_expression: // expression
      case symbol_kind::S_no_struct_expression: // no_struct_expression
      case symbol_kind::S_static_method_call: // static_method_call
      case symbol_kind::S_primary_no_struct: // primary_no_struct
      case symbol_kind::S_literal: // literal
      case symbol_kind::S_prototype_literal: // prototype_literal
        value.move< std::unique_ptr<fin::Expression> > (std::move (that.value));
        break;

      case symbol_kind::S_generic_param: // generic_param
        value.move< std::unique_ptr<fin::GenericParam> > (std::move (that.value));
        break;

      case symbol_kind::S_implements_body_content: // implements_body_content
        value.move< std::unique_ptr<fin::ImplementsBlock> > (std::move (that.value));
        break;

      case symbol_kind::S_interface_body_content: // interface_body_content
        value.move< std::unique_ptr<fin::InterfaceDeclaration> > (std::move (that.value));
        break;

      case symbol_kind::S_param: // param
        value.move< std::unique_ptr<fin::Parameter> > (std::move (that.value));
        break;

      case symbol_kind::S_program: // program
        value.move< std::unique_ptr<fin::Program> > (std::move (that.value));
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
        value.move< std::unique_ptr<fin::Statement> > (std::move (that.value));
        break;

      case symbol_kind::S_struct_body_content: // struct_body_content
        value.move< std::unique_ptr<fin::StructDeclaration> > (std::move (that.value));
        break;

      case symbol_kind::S_implements_opt: // implements_opt
      case symbol_kind::S_type: // type
      case symbol_kind::S_type_no_annot: // type_no_annot
      case symbol_kind::S_base_type: // base_type
      case symbol_kind::S_fn_type: // fn_type
      case symbol_kind::S_pointer_type: // pointer_type
      case symbol_kind::S_array_type: // array_type
        value.move< std::unique_ptr<fin::TypeNode> > (std::move (that.value));
        break;

      case symbol_kind::S_macro_param_list: // macro_param_list
        value.move< std::vector<fin::MacroParam> > (std::move (that.value));
        break;

      case symbol_kind::S_macro_rules: // macro_rules
        value.move< std::vector<fin::MacroRule> > (std::move (that.value));
        break;

      case symbol_kind::S_field_assignments: // field_assignments
      case symbol_kind::S_enum_values: // enum_values
        value.move< std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > (std::move (that.value));
        break;

      case symbol_kind::S_prototype_elements: // prototype_elements
        value.move< std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > (std::move (that.value));
        break;

      case symbol_kind::S_import_list: // import_list
        value.move< std::vector<std::string> > (std::move (that.value));
        break;

      case symbol_kind::S_attributes_opt: // attributes_opt
      case symbol_kind::S_attribute_list: // attribute_list
        value.move< std::vector<std::unique_ptr<fin::Attribute>> > (std::move (that.value));
        break;

      case symbol_kind::S_arguments: // arguments
      case symbol_kind::S_expression_list: // expression_list
      case symbol_kind::S_macro_arg_item: // macro_arg_item
      case symbol_kind::S_macro_arg_list_body: // macro_arg_list_body
      case symbol_kind::S_macro_arguments: // macro_arguments
        value.move< std::vector<std::unique_ptr<fin::Expression>> > (std::move (that.value));
        break;

      case symbol_kind::S_generic_params_opt: // generic_params_opt
      case symbol_kind::S_generic_param_list: // generic_param_list
      case symbol_kind::S_operator_generics_opt: // operator_generics_opt
        value.move< std::vector<std::unique_ptr<fin::GenericParam>> > (std::move (that.value));
        break;

      case symbol_kind::S_operator_params_opt: // operator_params_opt
      case symbol_kind::S_params: // params
      case symbol_kind::S_param_list: // param_list
        value.move< std::vector<std::unique_ptr<fin::Parameter>> > (std::move (that.value));
        break;

      case symbol_kind::S_statements: // statements
      case symbol_kind::S_block_stmts: // block_stmts
        value.move< std::vector<std::unique_ptr<fin::Statement>> > (std::move (that.value));
        break;

      case symbol_kind::S_inheritance_opt: // inheritance_opt
      case symbol_kind::S_type_list: // type_list
        value.move< std::vector<std::unique_ptr<fin::TypeNode>> > (std::move (that.value));
        break;

      default:
        break;
    }

      }
#endif

      /// Copy constructor.
      basic_symbol (const basic_symbol& that);

      /// Constructors for typed symbols.
#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, location_type&& l)
        : Base (t)
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const location_type& l)
        : Base (t)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, bool&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const bool& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, fin::ASTTokenKind&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const fin::ASTTokenKind& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, fin::MacroParam&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const fin::MacroParam& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, fin::MacroRule&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const fin::MacroRule& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::pair<std::string, std::unique_ptr<fin::Expression>>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::pair<std::string, std::unique_ptr<fin::Expression>>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::string&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::string& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::ASTNode>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::ASTNode>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::Attribute>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::Attribute>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::Block>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::Block>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::Expression>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::Expression>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::GenericParam>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::GenericParam>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::ImplementsBlock>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::ImplementsBlock>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::InterfaceDeclaration>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::InterfaceDeclaration>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::Parameter>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::Parameter>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::Program>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::Program>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::Statement>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::Statement>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::StructDeclaration>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::StructDeclaration>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::unique_ptr<fin::TypeNode>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::unique_ptr<fin::TypeNode>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::vector<fin::MacroParam>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::vector<fin::MacroParam>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::vector<fin::MacroRule>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::vector<fin::MacroRule>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::vector<std::string>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::vector<std::string>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::vector<std::unique_ptr<fin::Attribute>>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::vector<std::unique_ptr<fin::Attribute>>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::vector<std::unique_ptr<fin::Expression>>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::vector<std::unique_ptr<fin::Expression>>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::vector<std::unique_ptr<fin::GenericParam>>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::vector<std::unique_ptr<fin::GenericParam>>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::vector<std::unique_ptr<fin::Parameter>>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::vector<std::unique_ptr<fin::Parameter>>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::vector<std::unique_ptr<fin::Statement>>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::vector<std::unique_ptr<fin::Statement>>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

#if 201103L <= YY_CPLUSPLUS
      basic_symbol (typename Base::kind_type t, std::vector<std::unique_ptr<fin::TypeNode>>&& v, location_type&& l)
        : Base (t)
        , value (std::move (v))
        , location (std::move (l))
      {}
#else
      basic_symbol (typename Base::kind_type t, const std::vector<std::unique_ptr<fin::TypeNode>>& v, const location_type& l)
        : Base (t)
        , value (v)
        , location (l)
      {}
#endif

      /// Destroy the symbol.
      ~basic_symbol ()
      {
        clear ();
      }



      /// Destroy contents, and record that is empty.
      void clear () YY_NOEXCEPT
      {
        // User destructor.
        symbol_kind_type yykind = this->kind ();
        basic_symbol<Base>& yysym = *this;
        (void) yysym;
        switch (yykind)
        {
       default:
          break;
        }

        // Value type destructor.
switch (yykind)
    {
      case symbol_kind::S_visibility_opt: // visibility_opt
        value.template destroy< bool > ();
        break;

      case symbol_kind::S_operator_symbol: // operator_symbol
        value.template destroy< fin::ASTTokenKind > ();
        break;

      case symbol_kind::S_macro_param: // macro_param
        value.template destroy< fin::MacroParam > ();
        break;

      case symbol_kind::S_macro_rule: // macro_rule
        value.template destroy< fin::MacroRule > ();
        break;

      case symbol_kind::S_enum_value: // enum_value
        value.template destroy< std::pair<std::string, std::unique_ptr<fin::Expression>> > ();
        break;

      case symbol_kind::S_extern_params: // extern_params
        value.template destroy< std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > ();
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
        value.template destroy< std::string > ();
        break;

      case symbol_kind::S_struct_item_rest: // struct_item_rest
      case symbol_kind::S_interface_item_rest: // interface_item_rest
      case symbol_kind::S_implements_item_rest: // implements_item_rest
        value.template destroy< std::unique_ptr<fin::ASTNode> > ();
        break;

      case symbol_kind::S_attribute: // attribute
        value.template destroy< std::unique_ptr<fin::Attribute> > ();
        break;

      case symbol_kind::S_block: // block
        value.template destroy< std::unique_ptr<fin::Block> > ();
        break;

      case symbol_kind::S_super_expression: // super_expression
      case symbol_kind::S_lambda_expression: // lambda_expression
      case symbol_kind::S_expression: // expression
      case symbol_kind::S_no_struct_expression: // no_struct_expression
      case symbol_kind::S_static_method_call: // static_method_call
      case symbol_kind::S_primary_no_struct: // primary_no_struct
      case symbol_kind::S_literal: // literal
      case symbol_kind::S_prototype_literal: // prototype_literal
        value.template destroy< std::unique_ptr<fin::Expression> > ();
        break;

      case symbol_kind::S_generic_param: // generic_param
        value.template destroy< std::unique_ptr<fin::GenericParam> > ();
        break;

      case symbol_kind::S_implements_body_content: // implements_body_content
        value.template destroy< std::unique_ptr<fin::ImplementsBlock> > ();
        break;

      case symbol_kind::S_interface_body_content: // interface_body_content
        value.template destroy< std::unique_ptr<fin::InterfaceDeclaration> > ();
        break;

      case symbol_kind::S_param: // param
        value.template destroy< std::unique_ptr<fin::Parameter> > ();
        break;

      case symbol_kind::S_program: // program
        value.template destroy< std::unique_ptr<fin::Program> > ();
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
        value.template destroy< std::unique_ptr<fin::Statement> > ();
        break;

      case symbol_kind::S_struct_body_content: // struct_body_content
        value.template destroy< std::unique_ptr<fin::StructDeclaration> > ();
        break;

      case symbol_kind::S_implements_opt: // implements_opt
      case symbol_kind::S_type: // type
      case symbol_kind::S_type_no_annot: // type_no_annot
      case symbol_kind::S_base_type: // base_type
      case symbol_kind::S_fn_type: // fn_type
      case symbol_kind::S_pointer_type: // pointer_type
      case symbol_kind::S_array_type: // array_type
        value.template destroy< std::unique_ptr<fin::TypeNode> > ();
        break;

      case symbol_kind::S_macro_param_list: // macro_param_list
        value.template destroy< std::vector<fin::MacroParam> > ();
        break;

      case symbol_kind::S_macro_rules: // macro_rules
        value.template destroy< std::vector<fin::MacroRule> > ();
        break;

      case symbol_kind::S_field_assignments: // field_assignments
      case symbol_kind::S_enum_values: // enum_values
        value.template destroy< std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > ();
        break;

      case symbol_kind::S_prototype_elements: // prototype_elements
        value.template destroy< std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > ();
        break;

      case symbol_kind::S_import_list: // import_list
        value.template destroy< std::vector<std::string> > ();
        break;

      case symbol_kind::S_attributes_opt: // attributes_opt
      case symbol_kind::S_attribute_list: // attribute_list
        value.template destroy< std::vector<std::unique_ptr<fin::Attribute>> > ();
        break;

      case symbol_kind::S_arguments: // arguments
      case symbol_kind::S_expression_list: // expression_list
      case symbol_kind::S_macro_arg_item: // macro_arg_item
      case symbol_kind::S_macro_arg_list_body: // macro_arg_list_body
      case symbol_kind::S_macro_arguments: // macro_arguments
        value.template destroy< std::vector<std::unique_ptr<fin::Expression>> > ();
        break;

      case symbol_kind::S_generic_params_opt: // generic_params_opt
      case symbol_kind::S_generic_param_list: // generic_param_list
      case symbol_kind::S_operator_generics_opt: // operator_generics_opt
        value.template destroy< std::vector<std::unique_ptr<fin::GenericParam>> > ();
        break;

      case symbol_kind::S_operator_params_opt: // operator_params_opt
      case symbol_kind::S_params: // params
      case symbol_kind::S_param_list: // param_list
        value.template destroy< std::vector<std::unique_ptr<fin::Parameter>> > ();
        break;

      case symbol_kind::S_statements: // statements
      case symbol_kind::S_block_stmts: // block_stmts
        value.template destroy< std::vector<std::unique_ptr<fin::Statement>> > ();
        break;

      case symbol_kind::S_inheritance_opt: // inheritance_opt
      case symbol_kind::S_type_list: // type_list
        value.template destroy< std::vector<std::unique_ptr<fin::TypeNode>> > ();
        break;

      default:
        break;
    }

        Base::clear ();
      }

      /// The user-facing name of this symbol.
      const char *name () const YY_NOEXCEPT
      {
        return parser::symbol_name (this->kind ());
      }

      /// Backward compatibility (Bison 3.6).
      symbol_kind_type type_get () const YY_NOEXCEPT;

      /// Whether empty.
      bool empty () const YY_NOEXCEPT;

      /// Destructive move, \a s is emptied into this.
      void move (basic_symbol& s);

      /// The semantic value.
      value_type value;

      /// The location.
      location_type location;

    private:
#if YY_CPLUSPLUS < 201103L
      /// Assignment operator.
      basic_symbol& operator= (const basic_symbol& that);
#endif
    };

    /// Type access provider for token (enum) based symbols.
    struct by_kind
    {
      /// The symbol kind as needed by the constructor.
      typedef token_kind_type kind_type;

      /// Default constructor.
      by_kind () YY_NOEXCEPT;

#if 201103L <= YY_CPLUSPLUS
      /// Move constructor.
      by_kind (by_kind&& that) YY_NOEXCEPT;
#endif

      /// Copy constructor.
      by_kind (const by_kind& that) YY_NOEXCEPT;

      /// Constructor from (external) token numbers.
      by_kind (kind_type t) YY_NOEXCEPT;



      /// Record that this symbol is empty.
      void clear () YY_NOEXCEPT;

      /// Steal the symbol kind from \a that.
      void move (by_kind& that);

      /// The (internal) type number (corresponding to \a type).
      /// \a empty when empty.
      symbol_kind_type kind () const YY_NOEXCEPT;

      /// Backward compatibility (Bison 3.6).
      symbol_kind_type type_get () const YY_NOEXCEPT;

      /// The symbol kind.
      /// \a S_YYEMPTY when empty.
      symbol_kind_type kind_;
    };

    /// Backward compatibility for a private implementation detail (Bison 3.6).
    typedef by_kind by_type;

    /// "External" symbols: returned by the scanner.
    struct symbol_type : basic_symbol<by_kind>
    {
      /// Superclass.
      typedef basic_symbol<by_kind> super_type;

      /// Empty symbol.
      symbol_type () YY_NOEXCEPT {}

      /// Constructor for valueless symbols, and symbols from each type.
#if 201103L <= YY_CPLUSPLUS
      symbol_type (int tok, location_type l)
        : super_type (token_kind_type (tok), std::move (l))
#else
      symbol_type (int tok, const location_type& l)
        : super_type (token_kind_type (tok), l)
#endif
      {
#if !defined _MSC_VER || defined __clang__
        YY_ASSERT (tok == token::END
                   || (token::YYerror <= tok && tok <= token::YYUNDEF)
                   || (token::KW_LET <= tok && tok <= token::KW_IFX));
#endif
      }
#if 201103L <= YY_CPLUSPLUS
      symbol_type (int tok, std::string v, location_type l)
        : super_type (token_kind_type (tok), std::move (v), std::move (l))
#else
      symbol_type (int tok, const std::string& v, const location_type& l)
        : super_type (token_kind_type (tok), v, l)
#endif
      {
#if !defined _MSC_VER || defined __clang__
        YY_ASSERT ((token::IDENTIFIER <= tok && tok <= token::TYPE_ID));
#endif
      }
    };

    /// Build a parser object.
    parser (fin::DiagnosticEngine& diag_yyarg);
    virtual ~parser ();

#if 201103L <= YY_CPLUSPLUS
    /// Non copyable.
    parser (const parser&) = delete;
    /// Non copyable.
    parser& operator= (const parser&) = delete;
#endif

    /// Parse.  An alias for parse ().
    /// \returns  0 iff parsing succeeded.
    int operator() ();

    /// Parse.
    /// \returns  0 iff parsing succeeded.
    virtual int parse ();

#if YYDEBUG
    /// The current debugging stream.
    std::ostream& debug_stream () const YY_ATTRIBUTE_PURE;
    /// Set the current debugging stream.
    void set_debug_stream (std::ostream &);

    /// Type for debugging levels.
    typedef int debug_level_type;
    /// The current debugging level.
    debug_level_type debug_level () const YY_ATTRIBUTE_PURE;
    /// Set the current debugging level.
    void set_debug_level (debug_level_type l);
#endif

    /// Report a syntax error.
    /// \param loc    where the syntax error is found.
    /// \param msg    a description of the syntax error.
    virtual void error (const location_type& loc, const std::string& msg);

    /// Report a syntax error.
    void error (const syntax_error& err);

    /// The user-facing name of the symbol whose (internal) number is
    /// YYSYMBOL.  No bounds checking.
    static const char *symbol_name (symbol_kind_type yysymbol);

    // Implementation of make_symbol for each token kind.
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_END (location_type l)
      {
        return symbol_type (token::END, std::move (l));
      }
#else
      static
      symbol_type
      make_END (const location_type& l)
      {
        return symbol_type (token::END, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_YYerror (location_type l)
      {
        return symbol_type (token::YYerror, std::move (l));
      }
#else
      static
      symbol_type
      make_YYerror (const location_type& l)
      {
        return symbol_type (token::YYerror, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_YYUNDEF (location_type l)
      {
        return symbol_type (token::YYUNDEF, std::move (l));
      }
#else
      static
      symbol_type
      make_YYUNDEF (const location_type& l)
      {
        return symbol_type (token::YYUNDEF, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_IDENTIFIER (std::string v, location_type l)
      {
        return symbol_type (token::IDENTIFIER, std::move (v), std::move (l));
      }
#else
      static
      symbol_type
      make_IDENTIFIER (const std::string& v, const location_type& l)
      {
        return symbol_type (token::IDENTIFIER, v, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_INTEGER (std::string v, location_type l)
      {
        return symbol_type (token::INTEGER, std::move (v), std::move (l));
      }
#else
      static
      symbol_type
      make_INTEGER (const std::string& v, const location_type& l)
      {
        return symbol_type (token::INTEGER, v, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_FLOAT (std::string v, location_type l)
      {
        return symbol_type (token::FLOAT, std::move (v), std::move (l));
      }
#else
      static
      symbol_type
      make_FLOAT (const std::string& v, const location_type& l)
      {
        return symbol_type (token::FLOAT, v, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_STRING_LITERAL (std::string v, location_type l)
      {
        return symbol_type (token::STRING_LITERAL, std::move (v), std::move (l));
      }
#else
      static
      symbol_type
      make_STRING_LITERAL (const std::string& v, const location_type& l)
      {
        return symbol_type (token::STRING_LITERAL, v, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_CHAR_LITERAL (std::string v, location_type l)
      {
        return symbol_type (token::CHAR_LITERAL, std::move (v), std::move (l));
      }
#else
      static
      symbol_type
      make_CHAR_LITERAL (const std::string& v, const location_type& l)
      {
        return symbol_type (token::CHAR_LITERAL, v, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TYPE_ID (std::string v, location_type l)
      {
        return symbol_type (token::TYPE_ID, std::move (v), std::move (l));
      }
#else
      static
      symbol_type
      make_TYPE_ID (const std::string& v, const location_type& l)
      {
        return symbol_type (token::TYPE_ID, v, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_LET (location_type l)
      {
        return symbol_type (token::KW_LET, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_LET (const location_type& l)
      {
        return symbol_type (token::KW_LET, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_CONST (location_type l)
      {
        return symbol_type (token::KW_CONST, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_CONST (const location_type& l)
      {
        return symbol_type (token::KW_CONST, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_AUTO (location_type l)
      {
        return symbol_type (token::KW_AUTO, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_AUTO (const location_type& l)
      {
        return symbol_type (token::KW_AUTO, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_FUN (location_type l)
      {
        return symbol_type (token::KW_FUN, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_FUN (const location_type& l)
      {
        return symbol_type (token::KW_FUN, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_RETURN (location_type l)
      {
        return symbol_type (token::KW_RETURN, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_RETURN (const location_type& l)
      {
        return symbol_type (token::KW_RETURN, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_PUB (location_type l)
      {
        return symbol_type (token::KW_PUB, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_PUB (const location_type& l)
      {
        return symbol_type (token::KW_PUB, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_PRIV (location_type l)
      {
        return symbol_type (token::KW_PRIV, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_PRIV (const location_type& l)
      {
        return symbol_type (token::KW_PRIV, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_STRUCT (location_type l)
      {
        return symbol_type (token::KW_STRUCT, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_STRUCT (const location_type& l)
      {
        return symbol_type (token::KW_STRUCT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_ENUM (location_type l)
      {
        return symbol_type (token::KW_ENUM, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_ENUM (const location_type& l)
      {
        return symbol_type (token::KW_ENUM, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_INTERFACE (location_type l)
      {
        return symbol_type (token::KW_INTERFACE, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_INTERFACE (const location_type& l)
      {
        return symbol_type (token::KW_INTERFACE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_MACRO (location_type l)
      {
        return symbol_type (token::KW_MACRO, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_MACRO (const location_type& l)
      {
        return symbol_type (token::KW_MACRO, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_STATIC (location_type l)
      {
        return symbol_type (token::KW_STATIC, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_STATIC (const location_type& l)
      {
        return symbol_type (token::KW_STATIC, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_NULL (location_type l)
      {
        return symbol_type (token::KW_NULL, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_NULL (const location_type& l)
      {
        return symbol_type (token::KW_NULL, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_WHILE (location_type l)
      {
        return symbol_type (token::KW_WHILE, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_WHILE (const location_type& l)
      {
        return symbol_type (token::KW_WHILE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_FOR (location_type l)
      {
        return symbol_type (token::KW_FOR, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_FOR (const location_type& l)
      {
        return symbol_type (token::KW_FOR, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_FOREACH (location_type l)
      {
        return symbol_type (token::KW_FOREACH, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_FOREACH (const location_type& l)
      {
        return symbol_type (token::KW_FOREACH, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_BREAK (location_type l)
      {
        return symbol_type (token::KW_BREAK, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_BREAK (const location_type& l)
      {
        return symbol_type (token::KW_BREAK, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_CONTINUE (location_type l)
      {
        return symbol_type (token::KW_CONTINUE, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_CONTINUE (const location_type& l)
      {
        return symbol_type (token::KW_CONTINUE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_IF (location_type l)
      {
        return symbol_type (token::KW_IF, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_IF (const location_type& l)
      {
        return symbol_type (token::KW_IF, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_ELSE (location_type l)
      {
        return symbol_type (token::KW_ELSE, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_ELSE (const location_type& l)
      {
        return symbol_type (token::KW_ELSE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_ELSEIF (location_type l)
      {
        return symbol_type (token::KW_ELSEIF, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_ELSEIF (const location_type& l)
      {
        return symbol_type (token::KW_ELSEIF, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_IN (location_type l)
      {
        return symbol_type (token::KW_IN, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_IN (const location_type& l)
      {
        return symbol_type (token::KW_IN, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_TRY (location_type l)
      {
        return symbol_type (token::KW_TRY, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_TRY (const location_type& l)
      {
        return symbol_type (token::KW_TRY, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_CATCH (location_type l)
      {
        return symbol_type (token::KW_CATCH, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_CATCH (const location_type& l)
      {
        return symbol_type (token::KW_CATCH, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_BLAME (location_type l)
      {
        return symbol_type (token::KW_BLAME, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_BLAME (const location_type& l)
      {
        return symbol_type (token::KW_BLAME, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_SUPER (location_type l)
      {
        return symbol_type (token::KW_SUPER, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_SUPER (const location_type& l)
      {
        return symbol_type (token::KW_SUPER, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_SELF_TYPE (location_type l)
      {
        return symbol_type (token::KW_SELF_TYPE, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_SELF_TYPE (const location_type& l)
      {
        return symbol_type (token::KW_SELF_TYPE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_IMPORT (location_type l)
      {
        return symbol_type (token::KW_IMPORT, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_IMPORT (const location_type& l)
      {
        return symbol_type (token::KW_IMPORT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_AS (location_type l)
      {
        return symbol_type (token::KW_AS, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_AS (const location_type& l)
      {
        return symbol_type (token::KW_AS, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_FROM (location_type l)
      {
        return symbol_type (token::KW_FROM, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_FROM (const location_type& l)
      {
        return symbol_type (token::KW_FROM, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_NEW (location_type l)
      {
        return symbol_type (token::KW_NEW, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_NEW (const location_type& l)
      {
        return symbol_type (token::KW_NEW, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_DELETE (location_type l)
      {
        return symbol_type (token::KW_DELETE, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_DELETE (const location_type& l)
      {
        return symbol_type (token::KW_DELETE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_SIZEOF (location_type l)
      {
        return symbol_type (token::KW_SIZEOF, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_SIZEOF (const location_type& l)
      {
        return symbol_type (token::KW_SIZEOF, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_TYPEOF (location_type l)
      {
        return symbol_type (token::KW_TYPEOF, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_TYPEOF (const location_type& l)
      {
        return symbol_type (token::KW_TYPEOF, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_AS_PTR (location_type l)
      {
        return symbol_type (token::KW_AS_PTR, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_AS_PTR (const location_type& l)
      {
        return symbol_type (token::KW_AS_PTR, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_CAST (location_type l)
      {
        return symbol_type (token::KW_CAST, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_CAST (const location_type& l)
      {
        return symbol_type (token::KW_CAST, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_OPERATOR (location_type l)
      {
        return symbol_type (token::KW_OPERATOR, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_OPERATOR (const location_type& l)
      {
        return symbol_type (token::KW_OPERATOR, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_SPECIAL (location_type l)
      {
        return symbol_type (token::KW_SPECIAL, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_SPECIAL (const location_type& l)
      {
        return symbol_type (token::KW_SPECIAL, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_FN_TYPE (location_type l)
      {
        return symbol_type (token::KW_FN_TYPE, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_FN_TYPE (const location_type& l)
      {
        return symbol_type (token::KW_FN_TYPE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_DEFINE (location_type l)
      {
        return symbol_type (token::KW_DEFINE, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_DEFINE (const location_type& l)
      {
        return symbol_type (token::KW_DEFINE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_M1778 (location_type l)
      {
        return symbol_type (token::KW_M1778, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_M1778 (const location_type& l)
      {
        return symbol_type (token::KW_M1778, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_TYPE (location_type l)
      {
        return symbol_type (token::KW_TYPE, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_TYPE (const location_type& l)
      {
        return symbol_type (token::KW_TYPE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_CLASS (location_type l)
      {
        return symbol_type (token::KW_CLASS, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_CLASS (const location_type& l)
      {
        return symbol_type (token::KW_CLASS, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_IMPLEMENTS (location_type l)
      {
        return symbol_type (token::KW_IMPLEMENTS, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_IMPLEMENTS (const location_type& l)
      {
        return symbol_type (token::KW_IMPLEMENTS, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_ANY (location_type l)
      {
        return symbol_type (token::KW_ANY, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_ANY (const location_type& l)
      {
        return symbol_type (token::KW_ANY, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TYPE_INT (location_type l)
      {
        return symbol_type (token::TYPE_INT, std::move (l));
      }
#else
      static
      symbol_type
      make_TYPE_INT (const location_type& l)
      {
        return symbol_type (token::TYPE_INT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TYPE_FLOAT (location_type l)
      {
        return symbol_type (token::TYPE_FLOAT, std::move (l));
      }
#else
      static
      symbol_type
      make_TYPE_FLOAT (const location_type& l)
      {
        return symbol_type (token::TYPE_FLOAT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TYPE_DOUBLE (location_type l)
      {
        return symbol_type (token::TYPE_DOUBLE, std::move (l));
      }
#else
      static
      symbol_type
      make_TYPE_DOUBLE (const location_type& l)
      {
        return symbol_type (token::TYPE_DOUBLE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TYPE_BOOL (location_type l)
      {
        return symbol_type (token::TYPE_BOOL, std::move (l));
      }
#else
      static
      symbol_type
      make_TYPE_BOOL (const location_type& l)
      {
        return symbol_type (token::TYPE_BOOL, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TYPE_STRING (location_type l)
      {
        return symbol_type (token::TYPE_STRING, std::move (l));
      }
#else
      static
      symbol_type
      make_TYPE_STRING (const location_type& l)
      {
        return symbol_type (token::TYPE_STRING, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TYPE_CHAR (location_type l)
      {
        return symbol_type (token::TYPE_CHAR, std::move (l));
      }
#else
      static
      symbol_type
      make_TYPE_CHAR (const location_type& l)
      {
        return symbol_type (token::TYPE_CHAR, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TYPE_VOID (location_type l)
      {
        return symbol_type (token::TYPE_VOID, std::move (l));
      }
#else
      static
      symbol_type
      make_TYPE_VOID (const location_type& l)
      {
        return symbol_type (token::TYPE_VOID, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TYPE_LONG (location_type l)
      {
        return symbol_type (token::TYPE_LONG, std::move (l));
      }
#else
      static
      symbol_type
      make_TYPE_LONG (const location_type& l)
      {
        return symbol_type (token::TYPE_LONG, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_LPAREN (location_type l)
      {
        return symbol_type (token::LPAREN, std::move (l));
      }
#else
      static
      symbol_type
      make_LPAREN (const location_type& l)
      {
        return symbol_type (token::LPAREN, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_RPAREN (location_type l)
      {
        return symbol_type (token::RPAREN, std::move (l));
      }
#else
      static
      symbol_type
      make_RPAREN (const location_type& l)
      {
        return symbol_type (token::RPAREN, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_LBRACE (location_type l)
      {
        return symbol_type (token::LBRACE, std::move (l));
      }
#else
      static
      symbol_type
      make_LBRACE (const location_type& l)
      {
        return symbol_type (token::LBRACE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_RBRACE (location_type l)
      {
        return symbol_type (token::RBRACE, std::move (l));
      }
#else
      static
      symbol_type
      make_RBRACE (const location_type& l)
      {
        return symbol_type (token::RBRACE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_LBRACKET (location_type l)
      {
        return symbol_type (token::LBRACKET, std::move (l));
      }
#else
      static
      symbol_type
      make_LBRACKET (const location_type& l)
      {
        return symbol_type (token::LBRACKET, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_RBRACKET (location_type l)
      {
        return symbol_type (token::RBRACKET, std::move (l));
      }
#else
      static
      symbol_type
      make_RBRACKET (const location_type& l)
      {
        return symbol_type (token::RBRACKET, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_SEMICOLON (location_type l)
      {
        return symbol_type (token::SEMICOLON, std::move (l));
      }
#else
      static
      symbol_type
      make_SEMICOLON (const location_type& l)
      {
        return symbol_type (token::SEMICOLON, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_COLON (location_type l)
      {
        return symbol_type (token::COLON, std::move (l));
      }
#else
      static
      symbol_type
      make_COLON (const location_type& l)
      {
        return symbol_type (token::COLON, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_DOUBLE_COLON (location_type l)
      {
        return symbol_type (token::DOUBLE_COLON, std::move (l));
      }
#else
      static
      symbol_type
      make_DOUBLE_COLON (const location_type& l)
      {
        return symbol_type (token::DOUBLE_COLON, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_COMMA (location_type l)
      {
        return symbol_type (token::COMMA, std::move (l));
      }
#else
      static
      symbol_type
      make_COMMA (const location_type& l)
      {
        return symbol_type (token::COMMA, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_DOT (location_type l)
      {
        return symbol_type (token::DOT, std::move (l));
      }
#else
      static
      symbol_type
      make_DOT (const location_type& l)
      {
        return symbol_type (token::DOT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_ELLIPSIS (location_type l)
      {
        return symbol_type (token::ELLIPSIS, std::move (l));
      }
#else
      static
      symbol_type
      make_ELLIPSIS (const location_type& l)
      {
        return symbol_type (token::ELLIPSIS, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_AT (location_type l)
      {
        return symbol_type (token::AT, std::move (l));
      }
#else
      static
      symbol_type
      make_AT (const location_type& l)
      {
        return symbol_type (token::AT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_DOLLAR (location_type l)
      {
        return symbol_type (token::DOLLAR, std::move (l));
      }
#else
      static
      symbol_type
      make_DOLLAR (const location_type& l)
      {
        return symbol_type (token::DOLLAR, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_HASH (location_type l)
      {
        return symbol_type (token::HASH, std::move (l));
      }
#else
      static
      symbol_type
      make_HASH (const location_type& l)
      {
        return symbol_type (token::HASH, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TILDE (location_type l)
      {
        return symbol_type (token::TILDE, std::move (l));
      }
#else
      static
      symbol_type
      make_TILDE (const location_type& l)
      {
        return symbol_type (token::TILDE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_QUESTION (location_type l)
      {
        return symbol_type (token::QUESTION, std::move (l));
      }
#else
      static
      symbol_type
      make_QUESTION (const location_type& l)
      {
        return symbol_type (token::QUESTION, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_EQUAL (location_type l)
      {
        return symbol_type (token::EQUAL, std::move (l));
      }
#else
      static
      symbol_type
      make_EQUAL (const location_type& l)
      {
        return symbol_type (token::EQUAL, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_PLUSEQUAL (location_type l)
      {
        return symbol_type (token::PLUSEQUAL, std::move (l));
      }
#else
      static
      symbol_type
      make_PLUSEQUAL (const location_type& l)
      {
        return symbol_type (token::PLUSEQUAL, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_MINUSEQUAL (location_type l)
      {
        return symbol_type (token::MINUSEQUAL, std::move (l));
      }
#else
      static
      symbol_type
      make_MINUSEQUAL (const location_type& l)
      {
        return symbol_type (token::MINUSEQUAL, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_MULTEQUAL (location_type l)
      {
        return symbol_type (token::MULTEQUAL, std::move (l));
      }
#else
      static
      symbol_type
      make_MULTEQUAL (const location_type& l)
      {
        return symbol_type (token::MULTEQUAL, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_DIVEQUAL (location_type l)
      {
        return symbol_type (token::DIVEQUAL, std::move (l));
      }
#else
      static
      symbol_type
      make_DIVEQUAL (const location_type& l)
      {
        return symbol_type (token::DIVEQUAL, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_EQEQ (location_type l)
      {
        return symbol_type (token::EQEQ, std::move (l));
      }
#else
      static
      symbol_type
      make_EQEQ (const location_type& l)
      {
        return symbol_type (token::EQEQ, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_NOTEQ (location_type l)
      {
        return symbol_type (token::NOTEQ, std::move (l));
      }
#else
      static
      symbol_type
      make_NOTEQ (const location_type& l)
      {
        return symbol_type (token::NOTEQ, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_LT (location_type l)
      {
        return symbol_type (token::LT, std::move (l));
      }
#else
      static
      symbol_type
      make_LT (const location_type& l)
      {
        return symbol_type (token::LT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_GT (location_type l)
      {
        return symbol_type (token::GT, std::move (l));
      }
#else
      static
      symbol_type
      make_GT (const location_type& l)
      {
        return symbol_type (token::GT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_LTEQ (location_type l)
      {
        return symbol_type (token::LTEQ, std::move (l));
      }
#else
      static
      symbol_type
      make_LTEQ (const location_type& l)
      {
        return symbol_type (token::LTEQ, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_GTEQ (location_type l)
      {
        return symbol_type (token::GTEQ, std::move (l));
      }
#else
      static
      symbol_type
      make_GTEQ (const location_type& l)
      {
        return symbol_type (token::GTEQ, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_AND (location_type l)
      {
        return symbol_type (token::AND, std::move (l));
      }
#else
      static
      symbol_type
      make_AND (const location_type& l)
      {
        return symbol_type (token::AND, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_OR (location_type l)
      {
        return symbol_type (token::OR, std::move (l));
      }
#else
      static
      symbol_type
      make_OR (const location_type& l)
      {
        return symbol_type (token::OR, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_NOT (location_type l)
      {
        return symbol_type (token::NOT, std::move (l));
      }
#else
      static
      symbol_type
      make_NOT (const location_type& l)
      {
        return symbol_type (token::NOT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_PLUS (location_type l)
      {
        return symbol_type (token::PLUS, std::move (l));
      }
#else
      static
      symbol_type
      make_PLUS (const location_type& l)
      {
        return symbol_type (token::PLUS, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_MINUS (location_type l)
      {
        return symbol_type (token::MINUS, std::move (l));
      }
#else
      static
      symbol_type
      make_MINUS (const location_type& l)
      {
        return symbol_type (token::MINUS, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_MULT (location_type l)
      {
        return symbol_type (token::MULT, std::move (l));
      }
#else
      static
      symbol_type
      make_MULT (const location_type& l)
      {
        return symbol_type (token::MULT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_DIV (location_type l)
      {
        return symbol_type (token::DIV, std::move (l));
      }
#else
      static
      symbol_type
      make_DIV (const location_type& l)
      {
        return symbol_type (token::DIV, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_MOD (location_type l)
      {
        return symbol_type (token::MOD, std::move (l));
      }
#else
      static
      symbol_type
      make_MOD (const location_type& l)
      {
        return symbol_type (token::MOD, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_AMPERSAND (location_type l)
      {
        return symbol_type (token::AMPERSAND, std::move (l));
      }
#else
      static
      symbol_type
      make_AMPERSAND (const location_type& l)
      {
        return symbol_type (token::AMPERSAND, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_INCREMENT (location_type l)
      {
        return symbol_type (token::INCREMENT, std::move (l));
      }
#else
      static
      symbol_type
      make_INCREMENT (const location_type& l)
      {
        return symbol_type (token::INCREMENT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_DECREMENT (location_type l)
      {
        return symbol_type (token::DECREMENT, std::move (l));
      }
#else
      static
      symbol_type
      make_DECREMENT (const location_type& l)
      {
        return symbol_type (token::DECREMENT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_ARROW (location_type l)
      {
        return symbol_type (token::ARROW, std::move (l));
      }
#else
      static
      symbol_type
      make_ARROW (const location_type& l)
      {
        return symbol_type (token::ARROW, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_RARROW (location_type l)
      {
        return symbol_type (token::RARROW, std::move (l));
      }
#else
      static
      symbol_type
      make_RARROW (const location_type& l)
      {
        return symbol_type (token::RARROW, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_QUOTE (location_type l)
      {
        return symbol_type (token::KW_QUOTE, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_QUOTE (const location_type& l)
      {
        return symbol_type (token::KW_QUOTE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_HASH_FOR (location_type l)
      {
        return symbol_type (token::HASH_FOR, std::move (l));
      }
#else
      static
      symbol_type
      make_HASH_FOR (const location_type& l)
      {
        return symbol_type (token::HASH_FOR, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_HASH_INDEX (location_type l)
      {
        return symbol_type (token::HASH_INDEX, std::move (l));
      }
#else
      static
      symbol_type
      make_HASH_INDEX (const location_type& l)
      {
        return symbol_type (token::HASH_INDEX, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_SHIFTLEFT (location_type l)
      {
        return symbol_type (token::SHIFTLEFT, std::move (l));
      }
#else
      static
      symbol_type
      make_SHIFTLEFT (const location_type& l)
      {
        return symbol_type (token::SHIFTLEFT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_SHIFTRIGHT (location_type l)
      {
        return symbol_type (token::SHIFTRIGHT, std::move (l));
      }
#else
      static
      symbol_type
      make_SHIFTRIGHT (const location_type& l)
      {
        return symbol_type (token::SHIFTRIGHT, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_SHIFTLEFTEQUAL (location_type l)
      {
        return symbol_type (token::SHIFTLEFTEQUAL, std::move (l));
      }
#else
      static
      symbol_type
      make_SHIFTLEFTEQUAL (const location_type& l)
      {
        return symbol_type (token::SHIFTLEFTEQUAL, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_SHIFTRIGHTEQUAL (location_type l)
      {
        return symbol_type (token::SHIFTRIGHTEQUAL, std::move (l));
      }
#else
      static
      symbol_type
      make_SHIFTRIGHTEQUAL (const location_type& l)
      {
        return symbol_type (token::SHIFTRIGHTEQUAL, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_PIPE (location_type l)
      {
        return symbol_type (token::PIPE, std::move (l));
      }
#else
      static
      symbol_type
      make_PIPE (const location_type& l)
      {
        return symbol_type (token::PIPE, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_CARET (location_type l)
      {
        return symbol_type (token::CARET, std::move (l));
      }
#else
      static
      symbol_type
      make_CARET (const location_type& l)
      {
        return symbol_type (token::CARET, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_BACKTICK (location_type l)
      {
        return symbol_type (token::BACKTICK, std::move (l));
      }
#else
      static
      symbol_type
      make_BACKTICK (const location_type& l)
      {
        return symbol_type (token::BACKTICK, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TYPE_ANNOT_PREC (location_type l)
      {
        return symbol_type (token::TYPE_ANNOT_PREC, std::move (l));
      }
#else
      static
      symbol_type
      make_TYPE_ANNOT_PREC (const location_type& l)
      {
        return symbol_type (token::TYPE_ANNOT_PREC, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_UMINUS (location_type l)
      {
        return symbol_type (token::UMINUS, std::move (l));
      }
#else
      static
      symbol_type
      make_UMINUS (const location_type& l)
      {
        return symbol_type (token::UMINUS, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_ADDRESSOF_PREC (location_type l)
      {
        return symbol_type (token::ADDRESSOF_PREC, std::move (l));
      }
#else
      static
      symbol_type
      make_ADDRESSOF_PREC (const location_type& l)
      {
        return symbol_type (token::ADDRESSOF_PREC, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_DEREFERENCE_PREC (location_type l)
      {
        return symbol_type (token::DEREFERENCE_PREC, std::move (l));
      }
#else
      static
      symbol_type
      make_DEREFERENCE_PREC (const location_type& l)
      {
        return symbol_type (token::DEREFERENCE_PREC, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_HIGH_PREC (location_type l)
      {
        return symbol_type (token::HIGH_PREC, std::move (l));
      }
#else
      static
      symbol_type
      make_HIGH_PREC (const location_type& l)
      {
        return symbol_type (token::HIGH_PREC, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_TYPE_PREC (location_type l)
      {
        return symbol_type (token::TYPE_PREC, std::move (l));
      }
#else
      static
      symbol_type
      make_TYPE_PREC (const location_type& l)
      {
        return symbol_type (token::TYPE_PREC, l);
      }
#endif
#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_KW_IFX (location_type l)
      {
        return symbol_type (token::KW_IFX, std::move (l));
      }
#else
      static
      symbol_type
      make_KW_IFX (const location_type& l)
      {
        return symbol_type (token::KW_IFX, l);
      }
#endif


    class context
    {
    public:
      context (const parser& yyparser, const symbol_type& yyla);
      const symbol_type& lookahead () const YY_NOEXCEPT { return yyla_; }
      symbol_kind_type token () const YY_NOEXCEPT { return yyla_.kind (); }
      const location_type& location () const YY_NOEXCEPT { return yyla_.location; }

      /// Put in YYARG at most YYARGN of the expected tokens, and return the
      /// number of tokens stored in YYARG.  If YYARG is null, return the
      /// number of expected tokens (guaranteed to be less than YYNTOKENS).
      int expected_tokens (symbol_kind_type yyarg[], int yyargn) const;

    private:
      const parser& yyparser_;
      const symbol_type& yyla_;
    };

  private:
#if YY_CPLUSPLUS < 201103L
    /// Non copyable.
    parser (const parser&);
    /// Non copyable.
    parser& operator= (const parser&);
#endif


    /// Stored state numbers (used for stacks).
    typedef short state_type;

    /// The arguments of the error message.
    int yy_syntax_error_arguments_ (const context& yyctx,
                                    symbol_kind_type yyarg[], int yyargn) const;

    /// Generate an error message.
    /// \param yyctx     the context in which the error occurred.
    virtual std::string yysyntax_error_ (const context& yyctx) const;
    /// Compute post-reduction state.
    /// \param yystate   the current state
    /// \param yysym     the nonterminal to push on the stack
    static state_type yy_lr_goto_state_ (state_type yystate, int yysym);

    /// Whether the given \c yypact_ value indicates a defaulted state.
    /// \param yyvalue   the value to check
    static bool yy_pact_value_is_default_ (int yyvalue) YY_NOEXCEPT;

    /// Whether the given \c yytable_ value indicates a syntax error.
    /// \param yyvalue   the value to check
    static bool yy_table_value_is_error_ (int yyvalue) YY_NOEXCEPT;

    static const short yypact_ninf_;
    static const signed char yytable_ninf_;

    /// Convert a scanner token kind \a t to a symbol kind.
    /// In theory \a t should be a token_kind_type, but character literals
    /// are valid, yet not members of the token_kind_type enum.
    static symbol_kind_type yytranslate_ (int t) YY_NOEXCEPT;



    // Tables.
    // YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
    // STATE-NUM.
    static const short yypact_[];

    // YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
    // Performed when YYTABLE does not specify something else to do.  Zero
    // means the default is an error.
    static const short yydefact_[];

    // YYPGOTO[NTERM-NUM].
    static const short yypgoto_[];

    // YYDEFGOTO[NTERM-NUM].
    static const short yydefgoto_[];

    // YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
    // positive, shift that token.  If negative, reduce the rule whose
    // number is the opposite.  If YYTABLE_NINF, syntax error.
    static const short yytable_[];

    static const short yycheck_[];

    // YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
    // state STATE-NUM.
    static const unsigned char yystos_[];

    // YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.
    static const unsigned char yyr1_[];

    // YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.
    static const signed char yyr2_[];


#if YYDEBUG
    // YYRLINE[YYN] -- Source line where rule number YYN was defined.
    static const short yyrline_[];
    /// Report on the debug stream that the rule \a r is going to be reduced.
    virtual void yy_reduce_print_ (int r) const;
    /// Print the state stack on the debug stream.
    virtual void yy_stack_print_ () const;

    /// Debugging level.
    int yydebug_;
    /// Debug stream.
    std::ostream* yycdebug_;

    /// \brief Display a symbol kind, value and location.
    /// \param yyo    The output stream.
    /// \param yysym  The symbol.
    template <typename Base>
    void yy_print_ (std::ostream& yyo, const basic_symbol<Base>& yysym) const;
#endif

    /// \brief Reclaim the memory associated to a symbol.
    /// \param yymsg     Why this token is reclaimed.
    ///                  If null, print nothing.
    /// \param yysym     The symbol.
    template <typename Base>
    void yy_destroy_ (const char* yymsg, basic_symbol<Base>& yysym) const;

  private:
    /// Type access provider for state based symbols.
    struct by_state
    {
      /// Default constructor.
      by_state () YY_NOEXCEPT;

      /// The symbol kind as needed by the constructor.
      typedef state_type kind_type;

      /// Constructor.
      by_state (kind_type s) YY_NOEXCEPT;

      /// Copy constructor.
      by_state (const by_state& that) YY_NOEXCEPT;

      /// Record that this symbol is empty.
      void clear () YY_NOEXCEPT;

      /// Steal the symbol kind from \a that.
      void move (by_state& that);

      /// The symbol kind (corresponding to \a state).
      /// \a symbol_kind::S_YYEMPTY when empty.
      symbol_kind_type kind () const YY_NOEXCEPT;

      /// The state number used to denote an empty symbol.
      /// We use the initial state, as it does not have a value.
      enum { empty_state = 0 };

      /// The state.
      /// \a empty when empty.
      state_type state;
    };

    /// "Internal" symbol: element of the stack.
    struct stack_symbol_type : basic_symbol<by_state>
    {
      /// Superclass.
      typedef basic_symbol<by_state> super_type;
      /// Construct an empty symbol.
      stack_symbol_type ();
      /// Move or copy construction.
      stack_symbol_type (YY_RVREF (stack_symbol_type) that);
      /// Steal the contents from \a sym to build this.
      stack_symbol_type (state_type s, YY_MOVE_REF (symbol_type) sym);
#if YY_CPLUSPLUS < 201103L
      /// Assignment, needed by push_back by some old implementations.
      /// Moves the contents of that.
      stack_symbol_type& operator= (stack_symbol_type& that);

      /// Assignment, needed by push_back by other implementations.
      /// Needed by some other old implementations.
      stack_symbol_type& operator= (const stack_symbol_type& that);
#endif
    };

    /// A stack with random access from its top.
    template <typename T, typename S = std::vector<T> >
    class stack
    {
    public:
      // Hide our reversed order.
      typedef typename S::iterator iterator;
      typedef typename S::const_iterator const_iterator;
      typedef typename S::size_type size_type;
      typedef typename std::ptrdiff_t index_type;

      stack (size_type n = 200) YY_NOEXCEPT
        : seq_ (n)
      {}

#if 201103L <= YY_CPLUSPLUS
      /// Non copyable.
      stack (const stack&) = delete;
      /// Non copyable.
      stack& operator= (const stack&) = delete;
#endif

      /// Random access.
      ///
      /// Index 0 returns the topmost element.
      const T&
      operator[] (index_type i) const
      {
        return seq_[size_type (size () - 1 - i)];
      }

      /// Random access.
      ///
      /// Index 0 returns the topmost element.
      T&
      operator[] (index_type i)
      {
        return seq_[size_type (size () - 1 - i)];
      }

      /// Steal the contents of \a t.
      ///
      /// Close to move-semantics.
      void
      push (YY_MOVE_REF (T) t)
      {
        seq_.push_back (T ());
        operator[] (0).move (t);
      }

      /// Pop elements from the stack.
      void
      pop (std::ptrdiff_t n = 1) YY_NOEXCEPT
      {
        for (; 0 < n; --n)
          seq_.pop_back ();
      }

      /// Pop all elements from the stack.
      void
      clear () YY_NOEXCEPT
      {
        seq_.clear ();
      }

      /// Number of elements on the stack.
      index_type
      size () const YY_NOEXCEPT
      {
        return index_type (seq_.size ());
      }

      /// Iterator on top of the stack (going downwards).
      const_iterator
      begin () const YY_NOEXCEPT
      {
        return seq_.begin ();
      }

      /// Bottom of the stack.
      const_iterator
      end () const YY_NOEXCEPT
      {
        return seq_.end ();
      }

      /// Present a slice of the top of a stack.
      class slice
      {
      public:
        slice (const stack& stack, index_type range) YY_NOEXCEPT
          : stack_ (stack)
          , range_ (range)
        {}

        const T&
        operator[] (index_type i) const
        {
          return stack_[range_ - i];
        }

      private:
        const stack& stack_;
        index_type range_;
      };

    private:
#if YY_CPLUSPLUS < 201103L
      /// Non copyable.
      stack (const stack&);
      /// Non copyable.
      stack& operator= (const stack&);
#endif
      /// The wrapped container.
      S seq_;
    };


    /// Stack type.
    typedef stack<stack_symbol_type> stack_type;

    /// The stack.
    stack_type yystack_;

    /// Push a new state on the stack.
    /// \param m    a debug message to display
    ///             if null, no trace is output.
    /// \param sym  the symbol
    /// \warning the contents of \a s.value is stolen.
    void yypush_ (const char* m, YY_MOVE_REF (stack_symbol_type) sym);

    /// Push a new look ahead token on the state on the stack.
    /// \param m    a debug message to display
    ///             if null, no trace is output.
    /// \param s    the state
    /// \param sym  the symbol (for its value and location).
    /// \warning the contents of \a sym.value is stolen.
    void yypush_ (const char* m, state_type s, YY_MOVE_REF (symbol_type) sym);

    /// Pop \a n symbols from the stack.
    void yypop_ (int n = 1) YY_NOEXCEPT;

    /// Constants.
    enum
    {
      yylast_ = 2599,     ///< Last index in yytable_.
      yynnts_ = 81,  ///< Number of nonterminal symbols.
      yyfinal_ = 148 ///< Termination state number.
    };


    // User arguments.
    fin::DiagnosticEngine& diag;

  };

  inline
  parser::symbol_kind_type
  parser::yytranslate_ (int t) YY_NOEXCEPT
  {
    // YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to
    // TOKEN-NUM as returned by yylex.
    static
    const signed char
    translate_table[] =
    {
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119
    };
    // Last valid token kind.
    const int code_max = 374;

    if (t <= 0)
      return symbol_kind::S_YYEOF;
    else if (t <= code_max)
      return static_cast <symbol_kind_type> (translate_table[t]);
    else
      return symbol_kind::S_YYUNDEF;
  }

  // basic_symbol.
  template <typename Base>
  parser::basic_symbol<Base>::basic_symbol (const basic_symbol& that)
    : Base (that)
    , value ()
    , location (that.location)
  {
    switch (this->kind ())
    {
      case symbol_kind::S_visibility_opt: // visibility_opt
        value.copy< bool > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_operator_symbol: // operator_symbol
        value.copy< fin::ASTTokenKind > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_param: // macro_param
        value.copy< fin::MacroParam > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_rule: // macro_rule
        value.copy< fin::MacroRule > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_enum_value: // enum_value
        value.copy< std::pair<std::string, std::unique_ptr<fin::Expression>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_extern_params: // extern_params
        value.copy< std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > (YY_MOVE (that.value));
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
        value.copy< std::string > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_struct_item_rest: // struct_item_rest
      case symbol_kind::S_interface_item_rest: // interface_item_rest
      case symbol_kind::S_implements_item_rest: // implements_item_rest
        value.copy< std::unique_ptr<fin::ASTNode> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_attribute: // attribute
        value.copy< std::unique_ptr<fin::Attribute> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_block: // block
        value.copy< std::unique_ptr<fin::Block> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_super_expression: // super_expression
      case symbol_kind::S_lambda_expression: // lambda_expression
      case symbol_kind::S_expression: // expression
      case symbol_kind::S_no_struct_expression: // no_struct_expression
      case symbol_kind::S_static_method_call: // static_method_call
      case symbol_kind::S_primary_no_struct: // primary_no_struct
      case symbol_kind::S_literal: // literal
      case symbol_kind::S_prototype_literal: // prototype_literal
        value.copy< std::unique_ptr<fin::Expression> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_generic_param: // generic_param
        value.copy< std::unique_ptr<fin::GenericParam> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_implements_body_content: // implements_body_content
        value.copy< std::unique_ptr<fin::ImplementsBlock> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_interface_body_content: // interface_body_content
        value.copy< std::unique_ptr<fin::InterfaceDeclaration> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_param: // param
        value.copy< std::unique_ptr<fin::Parameter> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_program: // program
        value.copy< std::unique_ptr<fin::Program> > (YY_MOVE (that.value));
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
        value.copy< std::unique_ptr<fin::Statement> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_struct_body_content: // struct_body_content
        value.copy< std::unique_ptr<fin::StructDeclaration> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_implements_opt: // implements_opt
      case symbol_kind::S_type: // type
      case symbol_kind::S_type_no_annot: // type_no_annot
      case symbol_kind::S_base_type: // base_type
      case symbol_kind::S_fn_type: // fn_type
      case symbol_kind::S_pointer_type: // pointer_type
      case symbol_kind::S_array_type: // array_type
        value.copy< std::unique_ptr<fin::TypeNode> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_param_list: // macro_param_list
        value.copy< std::vector<fin::MacroParam> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_macro_rules: // macro_rules
        value.copy< std::vector<fin::MacroRule> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_field_assignments: // field_assignments
      case symbol_kind::S_enum_values: // enum_values
        value.copy< std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_prototype_elements: // prototype_elements
        value.copy< std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_import_list: // import_list
        value.copy< std::vector<std::string> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_attributes_opt: // attributes_opt
      case symbol_kind::S_attribute_list: // attribute_list
        value.copy< std::vector<std::unique_ptr<fin::Attribute>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_arguments: // arguments
      case symbol_kind::S_expression_list: // expression_list
      case symbol_kind::S_macro_arg_item: // macro_arg_item
      case symbol_kind::S_macro_arg_list_body: // macro_arg_list_body
      case symbol_kind::S_macro_arguments: // macro_arguments
        value.copy< std::vector<std::unique_ptr<fin::Expression>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_generic_params_opt: // generic_params_opt
      case symbol_kind::S_generic_param_list: // generic_param_list
      case symbol_kind::S_operator_generics_opt: // operator_generics_opt
        value.copy< std::vector<std::unique_ptr<fin::GenericParam>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_operator_params_opt: // operator_params_opt
      case symbol_kind::S_params: // params
      case symbol_kind::S_param_list: // param_list
        value.copy< std::vector<std::unique_ptr<fin::Parameter>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_statements: // statements
      case symbol_kind::S_block_stmts: // block_stmts
        value.copy< std::vector<std::unique_ptr<fin::Statement>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_inheritance_opt: // inheritance_opt
      case symbol_kind::S_type_list: // type_list
        value.copy< std::vector<std::unique_ptr<fin::TypeNode>> > (YY_MOVE (that.value));
        break;

      default:
        break;
    }

  }




  template <typename Base>
  parser::symbol_kind_type
  parser::basic_symbol<Base>::type_get () const YY_NOEXCEPT
  {
    return this->kind ();
  }


  template <typename Base>
  bool
  parser::basic_symbol<Base>::empty () const YY_NOEXCEPT
  {
    return this->kind () == symbol_kind::S_YYEMPTY;
  }

  template <typename Base>
  void
  parser::basic_symbol<Base>::move (basic_symbol& s)
  {
    super_type::move (s);
    switch (this->kind ())
    {
      case symbol_kind::S_visibility_opt: // visibility_opt
        value.move< bool > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_operator_symbol: // operator_symbol
        value.move< fin::ASTTokenKind > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_macro_param: // macro_param
        value.move< fin::MacroParam > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_macro_rule: // macro_rule
        value.move< fin::MacroRule > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_enum_value: // enum_value
        value.move< std::pair<std::string, std::unique_ptr<fin::Expression>> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_extern_params: // extern_params
        value.move< std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool> > (YY_MOVE (s.value));
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
        value.move< std::string > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_struct_item_rest: // struct_item_rest
      case symbol_kind::S_interface_item_rest: // interface_item_rest
      case symbol_kind::S_implements_item_rest: // implements_item_rest
        value.move< std::unique_ptr<fin::ASTNode> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_attribute: // attribute
        value.move< std::unique_ptr<fin::Attribute> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_block: // block
        value.move< std::unique_ptr<fin::Block> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_super_expression: // super_expression
      case symbol_kind::S_lambda_expression: // lambda_expression
      case symbol_kind::S_expression: // expression
      case symbol_kind::S_no_struct_expression: // no_struct_expression
      case symbol_kind::S_static_method_call: // static_method_call
      case symbol_kind::S_primary_no_struct: // primary_no_struct
      case symbol_kind::S_literal: // literal
      case symbol_kind::S_prototype_literal: // prototype_literal
        value.move< std::unique_ptr<fin::Expression> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_generic_param: // generic_param
        value.move< std::unique_ptr<fin::GenericParam> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_implements_body_content: // implements_body_content
        value.move< std::unique_ptr<fin::ImplementsBlock> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_interface_body_content: // interface_body_content
        value.move< std::unique_ptr<fin::InterfaceDeclaration> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_param: // param
        value.move< std::unique_ptr<fin::Parameter> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_program: // program
        value.move< std::unique_ptr<fin::Program> > (YY_MOVE (s.value));
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
        value.move< std::unique_ptr<fin::Statement> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_struct_body_content: // struct_body_content
        value.move< std::unique_ptr<fin::StructDeclaration> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_implements_opt: // implements_opt
      case symbol_kind::S_type: // type
      case symbol_kind::S_type_no_annot: // type_no_annot
      case symbol_kind::S_base_type: // base_type
      case symbol_kind::S_fn_type: // fn_type
      case symbol_kind::S_pointer_type: // pointer_type
      case symbol_kind::S_array_type: // array_type
        value.move< std::unique_ptr<fin::TypeNode> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_macro_param_list: // macro_param_list
        value.move< std::vector<fin::MacroParam> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_macro_rules: // macro_rules
        value.move< std::vector<fin::MacroRule> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_field_assignments: // field_assignments
      case symbol_kind::S_enum_values: // enum_values
        value.move< std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_prototype_elements: // prototype_elements
        value.move< std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_import_list: // import_list
        value.move< std::vector<std::string> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_attributes_opt: // attributes_opt
      case symbol_kind::S_attribute_list: // attribute_list
        value.move< std::vector<std::unique_ptr<fin::Attribute>> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_arguments: // arguments
      case symbol_kind::S_expression_list: // expression_list
      case symbol_kind::S_macro_arg_item: // macro_arg_item
      case symbol_kind::S_macro_arg_list_body: // macro_arg_list_body
      case symbol_kind::S_macro_arguments: // macro_arguments
        value.move< std::vector<std::unique_ptr<fin::Expression>> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_generic_params_opt: // generic_params_opt
      case symbol_kind::S_generic_param_list: // generic_param_list
      case symbol_kind::S_operator_generics_opt: // operator_generics_opt
        value.move< std::vector<std::unique_ptr<fin::GenericParam>> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_operator_params_opt: // operator_params_opt
      case symbol_kind::S_params: // params
      case symbol_kind::S_param_list: // param_list
        value.move< std::vector<std::unique_ptr<fin::Parameter>> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_statements: // statements
      case symbol_kind::S_block_stmts: // block_stmts
        value.move< std::vector<std::unique_ptr<fin::Statement>> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_inheritance_opt: // inheritance_opt
      case symbol_kind::S_type_list: // type_list
        value.move< std::vector<std::unique_ptr<fin::TypeNode>> > (YY_MOVE (s.value));
        break;

      default:
        break;
    }

    location = YY_MOVE (s.location);
  }

  // by_kind.
  inline
  parser::by_kind::by_kind () YY_NOEXCEPT
    : kind_ (symbol_kind::S_YYEMPTY)
  {}

#if 201103L <= YY_CPLUSPLUS
  inline
  parser::by_kind::by_kind (by_kind&& that) YY_NOEXCEPT
    : kind_ (that.kind_)
  {
    that.clear ();
  }
#endif

  inline
  parser::by_kind::by_kind (const by_kind& that) YY_NOEXCEPT
    : kind_ (that.kind_)
  {}

  inline
  parser::by_kind::by_kind (token_kind_type t) YY_NOEXCEPT
    : kind_ (yytranslate_ (t))
  {}



  inline
  void
  parser::by_kind::clear () YY_NOEXCEPT
  {
    kind_ = symbol_kind::S_YYEMPTY;
  }

  inline
  void
  parser::by_kind::move (by_kind& that)
  {
    kind_ = that.kind_;
    that.clear ();
  }

  inline
  parser::symbol_kind_type
  parser::by_kind::kind () const YY_NOEXCEPT
  {
    return kind_;
  }


  inline
  parser::symbol_kind_type
  parser::by_kind::type_get () const YY_NOEXCEPT
  {
    return this->kind ();
  }


#line 6 "/mnt/c/Users/m1778/Desktop/Fin/src/parser/parser.y"
} // fin
#line 4625 "parser.tab.h"




#endif // !YY_YY_PARSER_TAB_H_INCLUDED
