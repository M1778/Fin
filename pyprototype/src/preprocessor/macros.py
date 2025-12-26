from ..ast2.nodes import *
from copy import deepcopy
import re

class Macro:
    def __init__(self, name, args, body):
        self.name = name
        self.args = args
        self.body = body

    def expand(self, arg_values):
        if self.args is None:
            return self.body
        if len(arg_values) != len(self.args):
            raise Exception(
                f"Macro '{self.name}' expects {len(self.args)} args, got {len(arg_values)}."
            )
        result = self.body
        for param, val in zip(self.args, arg_values):
            result = re.sub(rf"\b{re.escape(param)}\b", val, result)
        return result


_macro_def_fn = re.compile(r"^\s*#cdef\s+([A-Za-z_]\w*)\s*\((.*?)\)\s+(.*)$")
_macro_def_obj = re.compile(r"^\s*#cdef\s+([A-Za-z_]\w*)\s+(.*)$")
_macro_call = re.compile(r"\b([A-Za-z_]\w*)\s*\(([^()]*)\)")


def preprocess_macros(text: str) -> str:
    macros = {}
    lines = text.splitlines()
    output = []

    for line in lines:
        fn_match = _macro_def_fn.match(line)
        obj_match = _macro_def_obj.match(line)

        if fn_match:
            name, args_str, body = fn_match.groups()
            args = [arg.strip() for arg in args_str.split(",") if arg.strip()]
            macros[name] = Macro(name, args, body)
        elif obj_match:
            name, body = obj_match.groups()
            macros[name] = Macro(name, None, body)
        else:

            def replacer(match):
                name = match.group(1)
                arg_str = match.group(2)
                if name in macros and macros[name].args is not None:
                    args = [a.strip() for a in arg_str.split(",")]
                    return macros[name].expand(args)
                return match.group(0)

            line = _macro_call.sub(replacer, line)

            for name, macro in macros.items():
                if macro.args is None:
                    line = re.sub(rf"\b{re.escape(name)}\b", macro.body, line)

            output.append(line)

    return "\n".join(output)


def substitute(node, bindings):
    if isinstance(node, str):
        return bindings.get(node, node)

    if isinstance(node, Literal):
        return deepcopy(node)

    if isinstance(node, list):
        return [substitute(n, bindings) for n in node]

    if isinstance(node, str):
        return bindings.get(node, node)

    if isinstance(node, MacroCall):
        return MacroCall(
            substitute(node.name, bindings),
            substitute(node.args, bindings),
        )

    if isinstance(node, FunctionCall):
        return FunctionCall(
            substitute(node.call_name, bindings),
            substitute(node.params, bindings),
        )

    if isinstance(node, StructMethodCall):
        return StructMethodCall(
            substitute(node.struct_name, bindings),
            substitute(node.method_name, bindings),
            substitute(node.params, bindings),
        )

    if isinstance(node, VariableDeclaration):
        return VariableDeclaration(
            node.is_mutable,
            substitute(node.identifier, bindings),
            substitute(node.type, bindings),
            substitute(node.value, bindings),
        )

    if isinstance(node, Assignment):
        return Assignment(
            substitute(node.identifier, bindings),
            node.operator,
            substitute(node.value, bindings),
        )

    if isinstance(node, AdditiveOperator):
        return AdditiveOperator(
            node.operator,
            substitute(node.left, bindings),
            substitute(node.right, bindings),
        )

    if isinstance(node, MultiplicativeOperator):
        return MultiplicativeOperator(
            node.operator,
            substitute(node.left, bindings),
            substitute(node.right, bindings),
        )

    if isinstance(node, ComparisonOperator):
        return ComparisonOperator(
            node.operator,
            substitute(node.left, bindings),
            substitute(node.right, bindings),
        )

    if isinstance(node, LogicalOperator):
        return LogicalOperator(
            node.operator,
            substitute(node.left, bindings),
            substitute(node.right, bindings),
        )

    if isinstance(node, UnaryOperator):
        return UnaryOperator(node.operator, substitute(node.operand, bindings))

    if isinstance(node, TypeConv):

        new_target = substitute(node.target_type, bindings)
        new_expr = substitute(node.expr, bindings)
        return TypeConv(new_target, new_expr)

    if isinstance(node, PostfixOperator):
        return PostfixOperator(node.operator, substitute(node.operand, bindings))

    if isinstance(node, ReturnStatement):
        return ReturnStatement(substitute(node.value, bindings))

    if isinstance(node, MemberAccess):
        return MemberAccess(
            substitute(node.struct_name, bindings),
            substitute(node.member_name, bindings),
        )

    if isinstance(node, StructInstantiation):
        return StructInstantiation(
            substitute(node.struct_name, bindings),
            substitute(node.field_assignments, bindings),
        )

    if isinstance(node, FieldAssignment):
        return FieldAssignment(
            substitute(node.identifier, bindings), substitute(node.value, bindings)
        )

    if isinstance(node, TypeOf):
        return TypeOf(substitute(node.expr, bindings))

    if isinstance(node, IfStatement):
        return IfStatement(
            substitute(node.condition, bindings),
            substitute(node.body, bindings),
            substitute(node.elifs, bindings),
            substitute(node.else_body, bindings),
        )

    if isinstance(node, WhileLoop):
        return WhileLoop(
            substitute(node.condition, bindings), substitute(node.body, bindings)
        )

    if isinstance(node, ForLoop):
        return ForLoop(
            substitute(node.init, bindings),
            substitute(node.condition, bindings),
            substitute(node.increment, bindings),
            substitute(node.body, bindings),
        )

    if isinstance(node, ForeachLoop):
        return ForeachLoop(
            substitute(node.identifier, bindings),
            substitute(node.var_type, bindings),
            substitute(node.iterable, bindings),
            substitute(node.body, bindings),
        )

    if isinstance(node, ControlStatement):
        return deepcopy(node)

    if isinstance(node, Program):
        return Program(substitute(node.statements, bindings))

    if isinstance(node, ModuleAccess):
        return ModuleAccess(
            substitute(node.alias, bindings), substitute(node.name, bindings)
        )

    if isinstance(node, QualifiedAccess):
        return QualifiedAccess(
            substitute(node.left, bindings), substitute(node.name, bindings)
        )

    if isinstance(node, EnumAccess):
        return EnumAccess(
            substitute(node.enum_name, bindings), substitute(node.value, bindings)
        )

    if isinstance(node, EnumDeclaration):
        return deepcopy(node)

    if isinstance(node, StructMember):
        return StructMember(
            substitute(node.identifier, bindings), substitute(node.var_type, bindings)
        )

    if isinstance(node, Parameter):
        return Parameter(
            substitute(node.identifier, bindings), substitute(node.var_type, bindings)
        )

    if isinstance(node, SpecialDeclaration):
        return deepcopy(node)

    if isinstance(node, MacroDeclaration):
        return deepcopy(node)

    if isinstance(node, FunctionDeclaration):
        return deepcopy(node)

    if isinstance(node, StructDeclaration):
        return deepcopy(node)

    if isinstance(node, Comment):
        return deepcopy(node)

    if (
        isinstance(node, ImportModule)
        or isinstance(node, ImportC)
        or isinstance(node, Extern)
    ):
        return deepcopy(node)

    raise Exception(f"Substitution not implemented for node: {node}")
