# =============================================================================
# Fin Programming Language Compiler
#
# Made with ❤️
#
# This project is genuinely built on love, dedication, and care.
# Fin exists not only as a compiler, but as a labor of passion —
# created for a lover, inspired by curiosity, perseverance, and belief
# in building something meaningful from the ground up.
#
# “What is made with love is never made in vain.”
# “Love is the reason this code exists; logic is how it survives.”
#
# -----------------------------------------------------------------------------
# Author: M1778
# Repository: https://github.com/M1778M/Fin
# Profile: https://github.com/M1778M/
#
# Socials:
#   Telegram: https://t.me/your_username_here
#   Instagram: https://instagram.com/your_username_here
#   X (Twitter): https://x.com/your_username_here
#
# -----------------------------------------------------------------------------
# Copyright (C) 2025 M1778
#
# This file is part of the Fin Programming Language Compiler.
#
# Fin is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fin is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fin.  If not, see <https://www.gnu.org/licenses/>.
#
# -----------------------------------------------------------------------------
# “Code fades. Love leaves a signature.”
# =============================================================================
# Program Basics:
class Node:
    lineno: int = 0
    col_offset: int = 0
    end_lineno: int = 0
    end_col_offset: int = 0
    filename: str = "<unknown>"

    def at(self, p, index=1):
        """
        Helper to attach location info from PLY slice.
        Usage in parser: p[0] = MyNode(...).at(p, 1)
        """
        self.lineno = p.lineno(index)
        self.lexpos = p.lexpos(index)
        return self

class BuiltinFunction(Node):
    def __init__(self):
        pass

# Mixin for nodes that support attributes
class Attributable:
    def __init__(self):
        self.attributes = {} # Map[str, Any]

    def add_attributes(self, attr_list):
        if not attr_list: return
        for attr in attr_list:
            self.attributes[attr.name] = attr.value

    def get_attr(self, name, default=None):
        return self.attributes.get(name, default)


class Program(Node):
    def __init__(self, statements):
        self.statements = statements

    def __repr__(self):
        t = ""
        for i in self.statements:
            t += str(i) + "\n\n"
        return f"Program({t})"

# Import/Module
class ImportC(Node):
    def __init__(self, path_or_name):
        self.path_or_name = path_or_name

    def __repr__(self):
        return f"ImportC({self.path_or_name})"


class ImportModule(Node):
    def __init__(self, source, is_package=False, targets=None, alias=None):
        self.source = source
        self.is_package = is_package
        self.targets = targets    
        self.alias = alias        
        
    def __repr__(self):
        return f"Import(src={self.source}, pkg={self.is_package}, targets={self.targets}, alias={self.alias})"

class ModuleAccess(Node):
    def __init__(self, alias, name):
        self.alias = alias
        self.name = name

    def __repr__(self):
        return f"ModuleAccess({self.alias}.{self.name})"

# Generic
class GenericTypeParameterDeclarationNode(Node):
    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return f"GenericTypeParameterDeclarationNode(Name='{self.name}')"

class GenericParam(Node):
    def __init__(self, name, constraint=None):
        self.name = name
        self.constraint = constraint # TypeNode (e.g. "StructWithVal")
    
    def __repr__(self):
        if self.constraint:
            return f"{self.name}:{self.constraint}"
        return self.name
    
class TypeParameterNode(Node):
    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return f"TypeParameterNode(Name='{self.name}')"


class SizeofNode(Node):
    def __init__(self, target_ast_node):

        self.target_ast_node = target_ast_node

    def __repr__(self):
        return f"SizeofNode(Target={self.target_ast_node})"

class DefineDeclaration(Node, Attributable):
    def __init__(self, name, params, return_type, is_vararg=False, attributes=None):
        Attributable.__init__(self)
        self.add_attributes(attributes)
        self.name = name
        self.params = params
        self.return_type = return_type
        self.is_vararg = is_vararg
    
    def __repr__(self): 
        return f"@define {self.name}({self.params}) -> {self.return_type}"
    
class AsPtrNode(Node):
    def __init__(self, expression_ast):
        self.expression_ast = expression_ast

    def __repr__(self):
        return f"AsPtrNode(Expression={self.expression_ast})"


class QualifiedAccess(Node):
    def __init__(self, left, name):
        self.left = left
        self.name = name

    def __repr__(self):
        return f"QualifiedAccess({self.left}, {self.name})"


class Literal(Node):
    def __init__(self, value):
        self.value = value

    def __repr__(self):
        return f"Literal({self.value})"


class Assignment(Node):
    def __init__(self, identifier, operator, value):
        self.identifier = identifier
        self.operator = operator
        self.value = value

    def __repr__(self):
        return f"Assignment(ID: {self.identifier}, Operator: {self.operator}, Value: {self.value})"


class VariableDeclaration(Node):
    def __init__(self, is_mutable, identifier, var_type, value):
        self.is_mutable = is_mutable
        self.identifier = identifier
        self.type = var_type
        self.value = value

    def __repr__(self):
        return f"VariableDeclaration(Mutable: {self.is_mutable}, ID: {self.identifier}, Type: {self.type}, Value: {self.value})"

    
class FunctionTypeNode(Node):
    def __init__(self, arg_types, return_type):
        self.arg_types = arg_types # List[TypeNode]
        self.return_type = return_type
    def __repr__(self): return f"FunctionType({self.arg_types} -> {self.return_type})"

class LambdaNode(Node):
    def __init__(self, params, return_type, body):
        self.params = params
        self.return_type = return_type
        self.body = body
    def __repr__(self): return f"Lambda({self.params} -> {self.return_type})"

  
class FunctionDeclaration(Node, Attributable):
    def __init__(self, name, params, return_type, body, is_static=False, type_parameters=None, is_vararg=False, visibility="private",attributes=None):
        Attributable.__init__(self)
        self.add_attributes(attributes)
        self.name = name
        self.params = params
        self.return_type = return_type
        self.body = body
        self.is_static = is_static
        self.type_parameters = type_parameters
        self.is_vararg = is_vararg
        self.visibility = visibility # "public" or "private"
    def __repr__(self):
        type_param_repr = ""
        if self.type_parameters:
            # [FIX] Convert generic params to string before joining
            type_param_repr = f"<{', '.join(str(t) for t in self.type_parameters)}>"
            
        static_prefix = "STATIC " if self.is_static else ""
        return f"{static_prefix}FunctionDeclaration{type_param_repr}(Name: {self.name}, Params: {self.params}, Visibility: {self.visibility}, Return Type: {self.return_type}, Body: {self.body}, Attributes: {self.attributes})"
    
class SuperNode(Node):
    def __repr__(self): return "Super"

  
    
class Parameter(Node):
    def __init__(self, identifier, var_type, default_value=None, is_vararg=False):
        self.identifier = identifier
        self.var_type = var_type
        self.default_value = default_value
        self.is_vararg = is_vararg
    
    def __repr__(self): 
        prefix = "..." if self.is_vararg else ""
        return f"Parameter({prefix}{self.identifier}, Type: {self.var_type}, Default: {self.default_value})"

  


class MacroParam(Node):
    def __init__(self, name, mode="expr", is_vararg=False):
        self.name = name
        self.mode = mode
        self.is_vararg = is_vararg
    def __repr__(self): return f"{self.name}:{self.mode}{'...' if self.is_vararg else ''}"

class MacroDeclaration(Node):
    def __init__(self, name, params, body):
        self.name = name
        self.params = params # List[MacroParam]
        self.body = body
    def __repr__(self): return f"@macro {self.name}({self.params})"
    
class MacroReturnStatement(Node):
    def __init__(self, value):
        self.value = value
    def __repr__(self): return f"@return {self.value}"

  

class MacroCall(Node):
    def __init__(self, name, args):
        self.name = name
        self.args = args or []

    def __repr__(self):
        return f"MacroCall({self.name}, {self.args})"

class Attribute(Node):
    def __init__(self, name, value=True):
        self.name = name
        self.value = value # Can be True (flag), string, int, etc.
    def __repr__(self): return f"#[{self.name}={self.value}]"


    
class SpecialDeclaration(Node, Attributable):
    def __init__(self, name, params, return_type, body, attributes=None):
        Attributable.__init__(self)
        self.add_attributes(attributes)
        self.name = name
        self.params = params
        self.return_type = return_type # [NEW]
        self.body = body
    
    def __repr__(self): 
        return f"SpecialDeclaration(Name: {self.name}, Params: {self.params}, Ret: {self.return_type})"

  
class SpecialCallNode(Node):
    def __init__(self, name, args):
        self.name = name
        self.args = args
    def __repr__(self): return f"SpecialCallNode(@{self.name}({self.args}))"

  

class FunctionCall(Node):
    def __init__(self, call_name, params, generic_args=None):
        self.call_name = call_name
        self.params = params
        self.generic_args = generic_args or [] # [NEW] List[TypeNode]
    
    def __repr__(self): 
        gen_str = f"::<{self.generic_args}>" if self.generic_args else ""
        return f"FunctionCall(Name: {self.call_name}{gen_str}, Params: {self.params})"


class StructMethodCall(Node):
    def __init__(self, struct_name, method_name, params):
        self.struct_name = struct_name
        self.method_name = method_name
        self.params = params

    def __repr__(self):
        return f"StructMethodCall(Struct: {self.struct_name}, Method: {self.method_name}, Params: {self.params})"


class StructDeclaration(Node, Attributable):
    def __init__(self, name, members, methods, constructor=None, destructor=None, operators=None, visibility="private", generic_params=None, parents=None, attributes=None):
        Attributable.__init__(self)
        self.add_attributes(attributes)
        self.name = name
        self.members = members
        self.methods = methods
        self.constructor = constructor
        self.destructor = destructor
        self.operators = operators or [] # New list for operators
        self.visibility = visibility
        self.generic_params = generic_params or []
        self.parents = parents or []
    def __repr__(self):
        gen_str = ""
        if self.generic_params:
            # [FIX] Convert generic params to string
            gen_str = f"<{', '.join(str(g) for g in self.generic_params)}>"
            
        parent_str = ""
        if self.parents:
            parent_str = f": {self.parents}"
            
        return f"StructDeclaration(Name: {self.name}{gen_str}{parent_str}, Members: {self.members}, Methods: {self.methods}, Visibility: {self.visibility})"
    def get_member_by_name(self, name):
        for member in self.members:
            if member.identifier == name:
                return member
        return None

    
class OperatorDeclaration(Node):
    def __init__(self, operator, params, return_type, body, visibility="public", generic_params=None):
        self.operator = operator
        self.params = params
        self.return_type = return_type
        self.body = body
        self.visibility = visibility
        self.generic_params = generic_params or []

    def __repr__(self):
        gen_str = ""
        if self.generic_params:
            # [FIX] Convert generic params to string
            gen_str = f"<{', '.join(str(g) for g in self.generic_params)}>"
            
        return f"Operator{gen_str}({self.operator}, Params={self.params})"

  
class DestructorDeclaration(Node):
    def __init__(self, body):
        self.body = body

    def __repr__(self):
        return f"Destructor(Body: {self.body})"
    
class InterfaceDeclaration(Node):
    def __init__(self, name, methods, visibility="private", generic_params=None):
        self.name = name
        self.methods = methods
        self.visibility = visibility
        self.generic_params = generic_params or []
    
    def __repr__(self): 
        gen_str = ""
        if self.generic_params:
            # [FIX] Convert generic params to string
            gen_str = f"<{', '.join(str(g) for g in self.generic_params)}>"
            
        return f"InterfaceDeclaration(Name: {self.name}{gen_str}, Methods: {self.methods}, Visibility: {self.visibility})"

class TryCatchNode(Node):
    def __init__(self, try_body, catch_var, catch_type, catch_body):
        self.try_body = try_body
        self.catch_var = catch_var # Identifier for the error variable
        self.catch_type = catch_type # Optional type constraint
        self.catch_body = catch_body

    def __repr__(self):
        return f"TryCatch(Try: {self.try_body}, Catch({self.catch_var}): {self.catch_body})"

class BlameNode(Node):
    def __init__(self, expression):
        self.expression = expression # The error object being thrown

    def __repr__(self):
        return f"Blame({self.expression})"

class GenericTypeNode(Node):
    def __init__(self, base_name, type_args):
        self.base_name = base_name
        self.type_args = type_args
    def __repr__(self):
        return f"GenericType({self.base_name}<{self.type_args}>)"

class FieldAssignment(Node):
    def __init__(self, identifier, value):
        self.identifier = identifier
        self.value = value

    def __repr__(self):
        return f"FieldAssignment(ID: {self.identifier}, Value: {self.value})"


class StructMember(Node):
    def __init__(self, identifier, var_type, visibility="private", default_value=None):
        self.identifier = identifier
        self.var_type = var_type
        self.visibility = visibility
        self.default_value = default_value # AST Node or None

    def __repr__(self):
        return f"StructMember(ID: {self.identifier}, Type: {self.var_type})"


class StructInstantiation(Node):
    def __init__(self, struct_name, field_assignments):
        self.struct_name = struct_name
        self.field_assignments = field_assignments

    def __repr__(self):
        return f"StructInstantiation(Name: {self.struct_name}, Fields: {self.field_assignments})"


class MemberAccess(Node):
    def __init__(self, struct_name, member_name):
        self.struct_name = struct_name
        self.member_name = member_name

    def __repr__(self):
        return f"MemberAccess(Struct: {self.struct_name}, Member: {self.member_name})"


class TypeConv(Node):
    def __init__(self, target_type, expr):
        self.target_type = target_type
        self.expr = expr

    def __repr__(self):
        return f"TypeConv(To: {self.target_type}, Expr: {self.expr})"


class TypeAnnotation(Node):
    def __init__(self, base: str, bits: int):
        self.base = base
        self.bits = bits

    def __repr__(self):
        return f"TypeAnnotation({self.base}, {self.bits})"


class NewExpressionNode(Node):
    def __init__(self, alloc_type_ast, init_args=None, init_fields=None):
        self.alloc_type_ast = alloc_type_ast
        self.init_args = init_args       # For basic types: new <int>(10) -> [Literal(10)]
        self.init_fields = init_fields   # For structs: new <MyStruct>{x:1} -> [FieldAssignment]
    def __repr__(self):
        return f"NewExpressionNode(AllocType={self.alloc_type_ast})"


class DeleteStatementNode(Node):
    def __init__(self, pointer_expr_ast):
        self.pointer_expr_ast = pointer_expr_ast

    def __repr__(self):
        return f"DeleteStatementNode(PointerExpr={self.pointer_expr_ast})"


class IfStatement(Node):
    def __init__(self, condition, body, elifs, else_body):
        self.condition = condition
        self.body = body
        self.elifs = elifs
        self.else_body = else_body

    def __repr__(self):
        return f"IfStatement(Condition: {self.condition}, Body: {self.body}, Elifs: {self.elifs}, Else: {self.else_body})"


class ForLoop(Node):
    def __init__(self, init, condition, increment, body):
        self.init = init
        self.condition = condition
        self.increment = increment
        self.body = body

    def __repr__(self):
        return f"ForLoop(Init: {self.init}, Condition: {self.condition}, Increment: {self.increment}, Body: {self.body})"


class WhileLoop(Node):
    def __init__(self, condition, body):
        self.condition = condition
        self.body = body

    def __repr__(self):
        return f"WhileLoop(Condition: {self.condition}, Body: {self.body})"


class ForeachLoop(Node):
    def __init__(self, identifier, var_type, iterable, body):
        self.identifier = identifier
        self.var_type = var_type
        self.iterable = iterable
        self.body = body

    def __repr__(self):
        return f"ForeachLoop(ID: {self.identifier}, Type: {self.var_type}, Iterable: {self.iterable}, Body: {self.body})"


class ControlStatement(Node):
    def __init__(self, control_type):
        self.control_type = control_type

    def __repr__(self):
        return f"ControlStatement(Type: {self.control_type})"


class EnumDeclaration(Node):
    def __init__(self, name, values):
        self.name = name
        self.values = values

    def __repr__(self):
        return f"EnumDeclaration(Name: {self.name}, Values: {self.values})"


class EnumAccess(Node):
    def __init__(self, enum_name, value):
        self.enum_name = enum_name
        self.value = value

    def __repr__(self):
        return f"EnumAccess(Enum: {self.enum_name}, Value: {self.value})"


class LogicalOperator(Node):
    def __init__(self, operator, left, right):
        self.operator = operator
        self.left = left
        self.right = right

    def __repr__(self):
        return f"LogicalOperator(Operator: {self.operator}, Left: {self.left}, Right: {self.right})"


class ComparisonOperator(Node):
    def __init__(self, operator, left, right):
        self.operator = operator
        self.left = left
        self.right = right

    def __repr__(self):
        return f"ComparisonOperator(Operator: {self.operator}, Left: {self.left}, Right: {self.right})"


class UnaryOperator(Node):
    def __init__(self, operator, operand):
        self.operator = operator
        self.operand = operand

    def __repr__(self):
        return f"UnaryOperator(Operator: {self.operator}, Operand: {self.operand})"


class PostfixOperator(Node):
    def __init__(self, operator, operand):
        self.operator = operator
        self.operand = operand

    def __repr__(self):
        return f"PostfixOperator(Operator: {self.operator}, Operand: {self.operand})"


class AdditiveOperator(Node):
    def __init__(self, operator, left, right):
        self.operator = operator
        self.left = left
        self.right = right

    def __repr__(self):
        return f"AdditiveOperator(Operator: {self.operator}, Left: {self.left}, Right: {self.right})"


class MultiplicativeOperator(Node):
    def __init__(self, operator, left, right):
        self.operator = operator
        self.left = left
        self.right = right

    def __repr__(self):
        return f"MultiplicativeOperator(Operator: {self.operator}, Left: {self.left}, Right: {self.right})"


class TypeOf(Node):
    def __init__(self, expr):
        self.expr = expr

    def __repr__(self):
        return f"TypeOf({self.expr})"


class ArrayTypeNode(Node):
    def __init__(self, element_type, size_expr=None):
        self.element_type = element_type
        self.size_expr = size_expr

    def __repr__(self):
        size_str = f", SizeExpr={self.size_expr}" if self.size_expr else ""
        return f"ArrayTypeNode(ElementType={self.element_type}{size_str})"


class PointerTypeNode(Node):
    def __init__(self, pointee_type):
        self.pointee_type = pointee_type

    def __repr__(self):
        return f"PointerTypeNode(PointeeType={self.pointee_type})"


class ArrayLiteralNode(Node):
    def __init__(self, elements):
        self.elements = elements

    def __repr__(self):
        return f"ArrayLiteralNode(Elements={self.elements})"


class ArrayIndexNode(Node):
    def __init__(self, array_expr, index_expr):
        self.array_expr = array_expr
        self.index_expr = index_expr

    def __repr__(self):
        return f"ArrayIndexNode(Array={self.array_expr}, Index={self.index_expr})"


class AddressOfNode(Node):
    def __init__(self, expression):
        self.expression = expression

    def __repr__(self):
        return f"AddressOfNode({self.expression})"


class DereferenceNode(Node):
    def __init__(self, expression):
        self.expression = expression

    def __repr__(self):
        return f"DereferenceNode({self.expression})"


class ReturnStatement(Node):
    def __init__(self, value):
        self.value = value

    def __repr__(self):
        return f"ReturnStatement(Value: {self.value})"

class ConstructorDeclaration(Node):
    def __init__(self, params, body, visibility="public"):
        self.params = params
        self.body = body
        self.visibility = visibility

    def __repr__(self):
        return f"Constructor(Params: {self.params}, Body: {self.body})"

class SuperCall(Node):
    def __init__(self, args):
        self.args = args
    def __repr__(self):
        return f"SuperCall(Args: {self.args})"
