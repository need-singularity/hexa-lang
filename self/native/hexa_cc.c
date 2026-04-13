// HEXA self-hosted compiler — combined native build
#include "runtime.c"

// ─── bt-53 fix: mangle user-level global identifiers that collide with
// runtime.c's internal enum values (TAG_INT, TAG_FLOAT, ..., TAG_CLOSURE).
// User code declares `let TAG_INT = 0` expecting a HexaVal global, but the
// generated C also includes runtime.c which defines `enum { TAG_INT=0, ... }`.
// Without mangling, the same symbol is declared as both an enum constant and
// a HexaVal variable → compile error. We prefix colliding user idents with
// `u_` so `let TAG_INT = 0` emits `HexaVal u_TAG_INT;` / `u_TAG_INT = hexa_int(0);`
// and any `Ident("TAG_INT")` reference emits `u_TAG_INT`. Runtime-internal
// references in hexa_cc.c's own emissions (e.g. `v.tag == TAG_INT` inside
// emitted C) stay unchanged because those are literal string fragments, not
// Ident AST nodes.
static const char* HEXA_RESERVED_RUNTIME_NAMES[] = {
    "TAG_INT", "TAG_FLOAT", "TAG_BOOL", "TAG_STR", "TAG_VOID",
    "TAG_ARRAY", "TAG_MAP", "TAG_FN", "TAG_CHAR", "TAG_CLOSURE",
    "main",
    NULL
};
static int hexa_name_is_reserved(const char* s) {
    if (!s) return 0;
    for (int i = 0; HEXA_RESERVED_RUNTIME_NAMES[i]; i++) {
        if (strcmp(s, HEXA_RESERVED_RUNTIME_NAMES[i]) == 0) return 1;
    }
    return 0;
}
HexaVal hexa_mangle_ident(HexaVal name) {
    if (name.tag != TAG_STR || !name.s) return name;
    if (hexa_name_is_reserved(name.s)) {
        return hexa_add(hexa_str("u_"), name);
    }
    return name;
}

// === parser + tokenizer ===

HexaVal empty_node(void);
HexaVal node_int_lit(HexaVal val);
HexaVal node_float_lit(HexaVal val);
HexaVal node_bool_lit(HexaVal val);
HexaVal node_string_lit(HexaVal val);
HexaVal node_char_lit(HexaVal val);
HexaVal node_ident(HexaVal name);
HexaVal node_wildcard(void);
HexaVal p_record_error(HexaVal line, HexaVal col, HexaVal message);
HexaVal p_synchronize(void);
HexaVal p_get_errors(void);
HexaVal p_has_errors(void);
HexaVal p_error_count(void);
HexaVal p_format_errors(void);
HexaVal p_peek(void);
HexaVal p_peek_kind(void);
HexaVal p_peek_ahead(HexaVal offset);
HexaVal p_at_end(void);
HexaVal p_advance(void);
HexaVal p_check(HexaVal expected_kind);
HexaVal p_expect(HexaVal expected_kind);
HexaVal p_expect_ident(void);
HexaVal p_skip_newlines(void);
HexaVal parse(HexaVal tokens);
HexaVal parse_strict(HexaVal tokens);
HexaVal parse_stmt(void);
HexaVal parse_let(void);
HexaVal parse_const(void);
HexaVal parse_static(void);
HexaVal parse_return(void);
HexaVal parse_proof_stmt(void);
HexaVal parse_assert_stmt(void);
HexaVal parse_use_decl(void);
HexaVal parse_mod_decl(void);
HexaVal parse_fn_decl(void);
HexaVal parse_type_annotation(void);
HexaVal parse_params(void);
HexaVal parse_struct_decl(void);
HexaVal parse_enum_decl(void);
HexaVal parse_trait_decl(void);
HexaVal parse_impl_block(void);
HexaVal parse_for_stmt(void);
HexaVal parse_while_stmt(void);
HexaVal parse_loop_stmt(void);
HexaVal parse_if_expr(void);
HexaVal parse_match_expr(void);
HexaVal parse_match_pattern(void);
HexaVal parse_block(void);
HexaVal parse_expr(void);
HexaVal parse_null_coal(void);
HexaVal parse_or(void);
HexaVal parse_and(void);
HexaVal parse_comparison(void);
HexaVal parse_addition(void);
HexaVal parse_multiplication(void);
HexaVal parse_unary(void);
HexaVal parse_postfix(void);
HexaVal parse_args(void);
HexaVal parse_primary(void);
HexaVal parse_try_catch(void);
HexaVal parse_throw_stmt(void);
HexaVal parse_panic_stmt(void);
HexaVal parse_recover_stmt(void);
HexaVal parse_drop_stmt(void);
HexaVal parse_spawn_stmt(void);
HexaVal parse_scope_stmt(void);
HexaVal parse_select_stmt(void);
HexaVal parse_async_fn(void);
HexaVal parse_attribute_prefix(void);
HexaVal parse_extern_fn(void);
HexaVal parse_type_alias(void);
HexaVal parse_atomic_let(void);
HexaVal parse_break_stmt(void);
HexaVal parse_continue_stmt(void);
HexaVal parse_yield_stmt(void);
HexaVal parse_comptime_stmt(void);
HexaVal parse_intent_stmt(void);
HexaVal parse_verify_stmt(void);
HexaVal parse_generate_stmt(void);
HexaVal parse_optimize_stmt(void);
HexaVal parse_effect_decl(void);
HexaVal parse_pure_fn(void);
HexaVal parse_macro_def(void);
HexaVal parse_derive_decl(void);
HexaVal parse_theorem_stmt(void);
HexaVal parse_handle_stmt(void);
HexaVal parse_visibility(void);
HexaVal parse_type_params(void);
HexaVal parse_where_clauses(void);
HexaVal ast_to_string(HexaVal node, HexaVal indent);
HexaVal print_ast(HexaVal stmts);
HexaVal is_keyword(HexaVal word);
HexaVal keyword_kind(HexaVal word);
HexaVal is_ident_start(HexaVal ch);
HexaVal is_ident_char(HexaVal ch);
HexaVal tokenize(HexaVal source);

HexaVal p_tokens;
HexaVal p_pos;
HexaVal p_pending_symbol;
HexaVal p_pending_link;
HexaVal p_pending_attrs;
HexaVal p_errors;
HexaVal p_max_errors;
/* rt-35 fix: disambiguate struct-literal from control-flow block.
 * When parsing the condition expression of if/while or the scrutinee of match,
 * or the iter-expr of for, we must NOT interpret `IDENT {` as a struct
 * literal, otherwise `if t == TAG_INT { return "int" }` is parsed as
 * `if (t == TAG_INT { return: "int" })` (a struct init with return: "int").
 * Set to 1 while parsing these contexts; parse_primary consults it. */
int p_no_struct_lit = 0;

HexaVal Token(HexaVal kind, HexaVal value, HexaVal line, HexaVal col) {
    HexaVal __s = hexa_map_new();
    __s = hexa_map_set(__s, "__type__", hexa_str("Token"));
    __s = hexa_map_set(__s, "kind", kind);
    __s = hexa_map_set(__s, "value", value);
    __s = hexa_map_set(__s, "line", line);
    __s = hexa_map_set(__s, "col", col);
    return __s;
}


HexaVal empty_node(void) {
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal node_int_lit(HexaVal val) {
    HexaVal n = empty_node();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("IntLit")), "name", hexa_str("")), "value", val), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal node_float_lit(HexaVal val) {
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("FloatLit")), "name", hexa_str("")), "value", val), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal node_bool_lit(HexaVal val) {
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("BoolLit")), "name", hexa_str("")), "value", val), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal node_string_lit(HexaVal val) {
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("StringLit")), "name", hexa_str("")), "value", val), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal node_char_lit(HexaVal val) {
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("CharLit")), "name", hexa_str("")), "value", val), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal node_ident(HexaVal name) {
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Ident")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal node_wildcard(void) {
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Wildcard")), "name", hexa_str("_")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal p_record_error(HexaVal line, HexaVal col, HexaVal message) {
    if (hexa_truthy(hexa_bool((hexa_int(hexa_len(p_errors))).i < (p_max_errors).i))) {
        p_errors = hexa_array_push(p_errors, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "line", line), "col", col), "message", message));
    }
    return hexa_void();
}


HexaVal p_synchronize(void) {
    while (hexa_truthy(hexa_bool(!hexa_truthy(p_at_end())))) {
        HexaVal cur = p_peek();
        if (hexa_truthy(hexa_eq(hexa_map_get(cur, "kind"), hexa_str("Newline")))) {
            p_advance();
            HexaVal next = p_peek();
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Let")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Const")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Static")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Fn")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Struct")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Enum")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("For")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("While")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Loop")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Return")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("If")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Match")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Impl")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Trait")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Use")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Mod")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Try")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Assert")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Proof")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("At")))) {
                return hexa_bool(1);
            }
            if (hexa_truthy(hexa_eq(hexa_map_get(next, "kind"), hexa_str("Eof")))) {
                return hexa_bool(1);
            }
        } else {
            p_advance();
        }
    }
    return hexa_bool(0);
    return hexa_void();
}


HexaVal p_get_errors(void) {
    return p_errors;
    return hexa_void();
}


HexaVal p_has_errors(void) {
    return hexa_bool((hexa_int(hexa_len(p_errors))).i > (hexa_int(0)).i);
    return hexa_void();
}


HexaVal p_error_count(void) {
    return hexa_int(hexa_len(p_errors));
    return hexa_void();
}


HexaVal p_format_errors(void) {
    HexaVal lines = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(p_errors))).i))) {
        HexaVal e = hexa_index_get(p_errors, i);
        lines = hexa_array_push(lines, hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("  error["), hexa_to_string(hexa_add(i, hexa_int(1)))), hexa_str("] ")), hexa_to_string(hexa_map_get(e, "line"))), hexa_str(":")), hexa_to_string(hexa_map_get(e, "col"))), hexa_str(" — ")), hexa_map_get(e, "message")));
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_str_join(lines, hexa_str("\n"));
    return hexa_void();
}


HexaVal p_peek(void) {
    if (hexa_truthy(hexa_bool((p_pos).i >= (hexa_int(hexa_len(p_tokens))).i))) {
        return Token(hexa_str("Eof"), hexa_str(""), hexa_int(0), hexa_int(0));
    }
    return hexa_index_get(p_tokens, p_pos);
    return hexa_void();
}


HexaVal p_peek_kind(void) {
    return hexa_map_get(p_peek(), "kind");
    return hexa_void();
}


HexaVal p_peek_ahead(HexaVal offset) {
    HexaVal idx = hexa_add(p_pos, offset);
    if (hexa_truthy(hexa_bool((idx).i >= (hexa_int(hexa_len(p_tokens))).i))) {
        return Token(hexa_str("Eof"), hexa_str(""), hexa_int(0), hexa_int(0));
    }
    return hexa_index_get(p_tokens, idx);
    return hexa_void();
}


HexaVal p_at_end(void) {
    return hexa_eq(p_peek_kind(), hexa_str("Eof"));
    return hexa_void();
}


HexaVal p_advance(void) {
    HexaVal tok = p_peek();
    p_pos = hexa_add(p_pos, hexa_int(1));
    return tok;
    return hexa_void();
}


HexaVal p_check(HexaVal expected_kind) {
    return hexa_eq(p_peek_kind(), expected_kind);
    return hexa_void();
}


HexaVal p_expect(HexaVal expected_kind) {
    if (hexa_truthy(p_check(expected_kind))) {
        return p_advance();
    }
    HexaVal tok = p_peek();
    HexaVal msg = hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("expected "), expected_kind), hexa_str(", got ")), hexa_map_get(tok, "kind")), hexa_str(" ('")), hexa_map_get(tok, "value")), hexa_str("')"));
    p_record_error(hexa_map_get(tok, "line"), hexa_map_get(tok, "col"), msg);
    hexa_println(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Parse error at "), hexa_to_string(hexa_map_get(tok, "line"))), hexa_str(":")), hexa_to_string(hexa_map_get(tok, "col"))), hexa_str(": ")), msg));
    return tok;
    return hexa_void();
}


HexaVal p_expect_ident(void) {
    HexaVal tok = p_peek();
    if (hexa_truthy(hexa_eq(hexa_map_get(tok, "kind"), hexa_str("Ident")))) {
        p_pos = hexa_add(p_pos, hexa_int(1));
        return hexa_map_get(tok, "value");
    }
    HexaVal msg2 = hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("expected identifier, got "), hexa_map_get(tok, "kind")), hexa_str(" ('")), hexa_map_get(tok, "value")), hexa_str("')"));
    p_record_error(hexa_map_get(tok, "line"), hexa_map_get(tok, "col"), msg2);
    hexa_println(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Parse error at "), hexa_to_string(hexa_map_get(tok, "line"))), hexa_str(":")), hexa_to_string(hexa_map_get(tok, "col"))), hexa_str(": ")), msg2));
    return hexa_str("");
    return hexa_void();
}


HexaVal p_skip_newlines(void) {
    /* rt-35 fix: treat `;` as a statement terminator equivalent to Newline,
     * so `a; b; c` inside a block parses as three statements. */
    while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Newline")))
        || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Semicolon")))) {
        p_pos = hexa_add(p_pos, hexa_int(1));
    }
    return hexa_void();
}


HexaVal parse(HexaVal tokens) {
    p_tokens = tokens;
    p_pos = hexa_int(0);
    p_pending_symbol = hexa_str("");
    p_pending_link = hexa_str("");
    p_errors = hexa_array_new();
    HexaVal stmts = hexa_array_new();
    p_skip_newlines();
    while (hexa_truthy(hexa_bool(!hexa_truthy(p_at_end())))) {
        HexaVal prev_pos = p_pos;
        HexaVal parse_ok = hexa_bool(1);
        {
            jmp_buf __jb; HexaVal __err = hexa_void();
            if (setjmp(__jb) == 0) {
                __hexa_try_push(&__jb);
                stmts = hexa_array_push(stmts, parse_stmt());
                __hexa_try_pop();
            } else {
                __err = __hexa_last_error();
                HexaVal parse_err = __err;
                HexaVal err_tok = p_peek();
                p_record_error(hexa_map_get(err_tok, "line"), hexa_map_get(err_tok, "col"), hexa_to_string(parse_err));
                p_synchronize();
                parse_ok = hexa_bool(0);
            }
        }
        if (hexa_truthy(parse_ok)) {
            if (hexa_truthy(hexa_eq(p_pos, prev_pos))) {
                p_advance();
            }
        }
        p_skip_newlines();
    }
    return stmts;
    return hexa_void();
}


HexaVal parse_strict(HexaVal tokens) {
    p_tokens = tokens;
    p_pos = hexa_int(0);
    p_pending_symbol = hexa_str("");
    p_pending_link = hexa_str("");
    p_errors = hexa_array_new();
    p_max_errors = hexa_int(50);
    HexaVal stmts = hexa_array_new();
    p_skip_newlines();
    while (hexa_truthy(hexa_bool(!hexa_truthy(p_at_end())))) {
        stmts = hexa_array_push(stmts, parse_stmt());
        p_skip_newlines();
    }
    return stmts;
    return hexa_void();
}


HexaVal parse_stmt(void) {
    p_skip_newlines();
    /* rt-35 fix: accept `pub` visibility prefix on declarations.
     * `pub let ...`, `pub fn ...`, `pub struct ...` etc. We consume `pub`
     * (and optional `pub(scope)`) and fall through to the normal stmt parse. */
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Pub")))) {
        p_advance();
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LParen")))) {
            p_advance();
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Ident")))) { p_advance(); }
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RParen")))) { p_advance(); }
        }
        p_skip_newlines();
    }
    while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("At")))) {
        p_advance();
        HexaVal attr_name = p_expect_ident();
        HexaVal attr_arg = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LParen")))) {
            p_advance();
            if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RParen")))))) {
                HexaVal tok = p_advance();
                attr_arg = hexa_map_get(tok, "value");
                while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RParen"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
                    p_advance();
                }
            }
            p_expect(hexa_str("RParen"));
        }
        if (hexa_truthy(hexa_eq(attr_name, hexa_str("symbol")))) {
            p_pending_symbol = attr_arg;
        } else {
            if (hexa_truthy(hexa_eq(attr_name, hexa_str("link")))) {
                p_pending_link = attr_arg;
            } else {
                if (hexa_truthy(hexa_eq(p_pending_attrs, hexa_str("")))) {
                    p_pending_attrs = attr_name;
                } else {
                    p_pending_attrs = hexa_add(hexa_add(p_pending_attrs, hexa_str(",")), attr_name);
                }
            }
        }
        p_skip_newlines();
    }
    HexaVal kind = p_peek_kind();
    if (hexa_truthy(hexa_eq(kind, hexa_str("Let")))) {
        return parse_let();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Const")))) {
        return parse_const();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Static")))) {
        return parse_static();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Fn")))) {
        return parse_fn_decl();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Struct")))) {
        return parse_struct_decl();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Enum")))) {
        return parse_enum_decl();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("For")))) {
        return parse_for_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("While")))) {
        return parse_while_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Loop")))) {
        return parse_loop_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Return")))) {
        return parse_return();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Proof")))) {
        return parse_proof_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Assert")))) {
        return parse_assert_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Impl")))) {
        return parse_impl_block();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Trait")))) {
        return parse_trait_decl();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Use")))) {
        return parse_use_decl();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Mod")))) {
        return parse_mod_decl();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Ident")))) {
        HexaVal tok = p_peek();
        if (hexa_truthy(hexa_eq(hexa_map_get(tok, "value"), hexa_str("import")))) {
            p_advance();
            HexaVal path = hexa_str("");
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("StringLit")))) {
                HexaVal str_tok = p_advance();
                path = hexa_map_get(str_tok, "value");
            }
            return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ImportStmt")), "name", path), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
        }
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Try")))) {
        return parse_try_catch();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Throw")))) {
        return parse_throw_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Panic")))) {
        return parse_panic_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Drop")))) {
        return parse_drop_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Spawn")))) {
        return parse_spawn_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Scope")))) {
        return parse_scope_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Select")))) {
        return parse_select_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Async")))) {
        return parse_async_fn();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Extern")))) {
        return parse_extern_fn();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Type")))) {
        return parse_type_alias();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Atomic")))) {
        return parse_atomic_let();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Break")))) {
        return parse_break_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Continue")))) {
        return parse_continue_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Yield")))) {
        return parse_yield_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Comptime")))) {
        return parse_comptime_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Intent")))) {
        return parse_intent_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Verify")))) {
        return parse_verify_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Generate")))) {
        return parse_generate_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Optimize")))) {
        return parse_optimize_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Effect")))) {
        return parse_effect_decl();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Pure")))) {
        return parse_pure_fn();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Macro")))) {
        return parse_macro_def();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Derive")))) {
        return parse_derive_decl();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Theorem")))) {
        return parse_theorem_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Handle")))) {
        return parse_handle_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Recover")))) {
        return parse_recover_stmt();
    }
    if (hexa_truthy(hexa_eq(kind, hexa_str("Attribute")))) {
        return parse_attribute_prefix();
    }
    HexaVal expr = parse_expr();
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eq")))) {
        p_advance();
        HexaVal rhs = parse_expr();
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("AssignStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", rhs), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ExprStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_let(void) {
    p_advance();
    HexaVal is_mut = hexa_bool(0);
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Mut")))) {
        p_advance();
        is_mut = hexa_bool(1);
    }
    HexaVal name = p_expect_ident();
    HexaVal typ = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Colon")))) {
        p_advance();
        typ = p_expect_ident();
    }
    p_expect(hexa_str("Eq"));
    HexaVal expr = parse_expr();
    HexaVal kind_str = hexa_str("LetStmt");
    if (hexa_truthy(is_mut)) {
        kind_str = hexa_str("LetMutStmt");
    }
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", kind_str), "name", name), "value", typ), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_const(void) {
    p_advance();
    HexaVal name = p_expect_ident();
    HexaVal typ = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Colon")))) {
        p_advance();
        typ = p_expect_ident();
    }
    p_expect(hexa_str("Eq"));
    HexaVal expr = parse_expr();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ConstStmt")), "name", name), "value", typ), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_static(void) {
    p_advance();
    HexaVal name = p_expect_ident();
    HexaVal typ = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Colon")))) {
        p_advance();
        typ = p_expect_ident();
    }
    p_expect(hexa_str("Eq"));
    HexaVal expr = parse_expr();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("StaticStmt")), "name", name), "value", typ), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_return(void) {
    p_advance();
    HexaVal k = p_peek_kind();
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("Newline"))) || hexa_truthy(hexa_eq(k, hexa_str("Eof"))))) || hexa_truthy(hexa_eq(k, hexa_str("RBrace")))))) {
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ReturnStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    HexaVal expr = parse_expr();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ReturnStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_proof_stmt(void) {
    p_advance();
    HexaVal name = p_expect_ident();
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ProofStmt")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_assert_stmt(void) {
    p_advance();
    HexaVal expr = parse_expr();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("AssertStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_use_decl(void) {
    p_advance();
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("StringLit")))) {
        HexaVal str_tok = p_advance();
        HexaVal str_val = hexa_map_get(str_tok, "value");
        HexaVal str_items = hexa_array_push(hexa_array_new(), str_val);
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("UseStmt")), "name", str_val), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", str_items), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    HexaVal path = hexa_array_new();
    path = hexa_array_push(path, p_expect_ident());
    while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("ColonColon")))) {
        p_advance();
        path = hexa_array_push(path, p_expect_ident());
    }
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("UseStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", path), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_mod_decl(void) {
    p_advance();
    HexaVal name = p_expect_ident();
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ModStmt")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_fn_decl(void) {
    p_advance();
    HexaVal name = p_expect_ident();
    HexaVal tparams = hexa_array_new();
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Lt")))) {
        tparams = parse_type_params();
    }
    p_expect(hexa_str("LParen"));
    HexaVal params = parse_params();
    p_expect(hexa_str("RParen"));
    HexaVal ret = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Arrow")))) {
        p_advance();
        ret = p_expect_ident();
    }
    HexaVal body = parse_block();
    HexaVal attrs = p_pending_attrs;
    p_pending_attrs = hexa_str("");
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("FnDecl")), "name", name), "value", attrs), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", params), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", tparams), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", ret), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_type_annotation(void) {
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Star")))) {
        p_advance();
        return hexa_add(hexa_str("*"), p_expect_ident());
    }
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LBracket")))) {
        p_advance();
        HexaVal inner = parse_type_annotation();
        p_expect(hexa_str("RBracket"));
        return hexa_add(hexa_add(hexa_str("["), inner), hexa_str("]"));
    }
    return p_expect_ident();
    return hexa_void();
}


HexaVal parse_params(void) {
    HexaVal params = hexa_array_new();
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RParen")))) {
        return params;
    }
    HexaVal name = p_expect_ident();
    HexaVal typ = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Colon")))) {
        p_advance();
        typ = parse_type_annotation();
    }
    params = hexa_array_push(params, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Param")), "name", name), "value", typ), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
    while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
        p_advance();
        HexaVal pname = p_expect_ident();
        HexaVal ptyp = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Colon")))) {
            p_advance();
            ptyp = parse_type_annotation();
        }
        params = hexa_array_push(params, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Param")), "name", pname), "value", ptyp), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
    }
    return params;
    return hexa_void();
}


HexaVal parse_struct_decl(void) {
    p_advance();
    HexaVal name = p_expect_ident();
    HexaVal tparams = hexa_array_new();
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Lt")))) {
        tparams = parse_type_params();
    }
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal fields = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        HexaVal fname = p_expect_ident();
        p_expect(hexa_str("Colon"));
        HexaVal ftype = p_expect_ident();
        fields = hexa_array_push(fields, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("StructField")), "name", fname), "value", ftype), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
            p_advance();
        }
        p_skip_newlines();
    }
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("StructDecl")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", fields), "items", tparams), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_enum_decl(void) {
    p_advance();
    HexaVal name = p_expect_ident();
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal variants = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        HexaVal vname = p_expect_ident();
        HexaVal vtypes = hexa_array_new();
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LParen")))) {
            p_advance();
            while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RParen"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
                vtypes = hexa_array_push(vtypes, p_expect_ident());
                if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
                    p_advance();
                }
            }
            p_expect(hexa_str("RParen"));
        }
        variants = hexa_array_push(variants, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("EnumVariant")), "name", vname), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", vtypes), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
            p_advance();
        }
        p_skip_newlines();
    }
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("EnumDecl")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", variants), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_trait_decl(void) {
    p_advance();
    HexaVal name = p_expect_ident();
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal methods = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        p_skip_newlines();
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace")))) {
        } else {
            p_expect(hexa_str("Fn"));
            HexaVal mname = p_expect_ident();
            p_expect(hexa_str("LParen"));
            HexaVal mparams = parse_params();
            p_expect(hexa_str("RParen"));
            HexaVal mret = hexa_str("");
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Arrow")))) {
                p_advance();
                mret = p_expect_ident();
            }
            HexaVal mbody = hexa_array_new();
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LBrace")))) {
                mbody = parse_block();
            }
            methods = hexa_array_push(methods, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("FnDecl")), "name", mname), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", mparams), "body", mbody), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", mret), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
            p_skip_newlines();
        }
    }
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("TraitDecl")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", methods);
    return hexa_void();
}


HexaVal parse_impl_block(void) {
    p_advance();
    HexaVal first_name = p_expect_ident();
    HexaVal trait_n = hexa_str("");
    HexaVal target_n = first_name;
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("For")))) {
        p_advance();
        trait_n = first_name;
        target_n = p_expect_ident();
    }
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal methods = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        p_skip_newlines();
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace")))) {
        } else {
            p_expect(hexa_str("Fn"));
            HexaVal mname = p_expect_ident();
            p_expect(hexa_str("LParen"));
            HexaVal mparams = parse_params();
            p_expect(hexa_str("RParen"));
            HexaVal mret = hexa_str("");
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Arrow")))) {
                p_advance();
                mret = p_expect_ident();
            }
            HexaVal mbody = parse_block();
            methods = hexa_array_push(methods, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("FnDecl")), "name", mname), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", mparams), "body", mbody), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", mret), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
            p_skip_newlines();
        }
    }
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ImplBlock")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", target_n), "trait_name", trait_n), "methods", methods);
    return hexa_void();
}


HexaVal parse_for_stmt(void) {
    p_advance();
    HexaVal var = p_expect_ident();
    HexaVal in_tok = p_advance();
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_map_get(in_tok, "value"), hexa_str("in")))))) {
        HexaVal for_msg = hexa_format_n(hexa_str("expected 'in' after for variable, got '{}'"), hexa_array_push(hexa_array_new(), hexa_map_get(in_tok, "value")));
        p_record_error(hexa_map_get(in_tok, "line"), hexa_map_get(in_tok, "col"), for_msg);
        hexa_println(hexa_add(hexa_str("Parse error: "), for_msg));
    }
    int __prev_nsl_for = p_no_struct_lit; p_no_struct_lit = 1;
    HexaVal iter = parse_expr();
    p_no_struct_lit = __prev_nsl_for;
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ForStmt")), "name", var), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", iter), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_while_stmt(void) {
    p_advance();
    int __prev_nsl = p_no_struct_lit; p_no_struct_lit = 1;
    HexaVal cond = parse_expr();
    p_no_struct_lit = __prev_nsl;
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("WhileStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", cond), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_loop_stmt(void) {
    p_advance();
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("LoopStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_if_expr(void) {
    p_advance();
    int __prev_nsl = p_no_struct_lit; p_no_struct_lit = 1;
    HexaVal cond = parse_expr();
    p_no_struct_lit = __prev_nsl;
    HexaVal then_b = parse_block();
    HexaVal saved = p_pos;
    p_skip_newlines();
    HexaVal else_b = hexa_array_new();
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Else")))) {
        p_advance();
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("If")))) {
            HexaVal nested = parse_if_expr();
            else_b = hexa_array_push(hexa_array_new(), hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ExprStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", nested), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
        } else {
            else_b = parse_block();
        }
    } else {
        p_pos = saved;
    }
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("IfExpr")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", cond), "then_body", then_b), "else_body", else_b), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_match_expr(void) {
    p_advance();
    int __prev_nsl_m = p_no_struct_lit; p_no_struct_lit = 1;
    HexaVal scrutinee = parse_expr();
    p_no_struct_lit = __prev_nsl_m;
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal arms = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        HexaVal pattern = parse_match_pattern();
        HexaVal guard = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("If")))) {
            p_advance();
            guard = parse_expr();
        }
        p_expect(hexa_str("Arrow"));
        HexaVal arm_body = hexa_array_new();
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LBrace")))) {
            arm_body = parse_block();
        } else {
            HexaVal arm_expr = parse_expr();
            arm_body = hexa_array_push(hexa_array_new(), hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ExprStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", arm_expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
        }
        arms = hexa_array_push(arms, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("MatchArm")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", pattern), "right", hexa_str("")), "cond", guard), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", arm_body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
            p_advance();
        }
        p_skip_newlines();
    }
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("MatchExpr")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", scrutinee), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", arms), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_match_pattern(void) {
    HexaVal tok = p_peek();
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(hexa_map_get(tok, "kind"), hexa_str("Ident"))) && hexa_truthy(hexa_eq(hexa_map_get(tok, "value"), hexa_str("_")))))) {
        p_advance();
        return node_wildcard();
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(hexa_map_get(tok, "kind"), hexa_str("Ident"))) && hexa_truthy(hexa_eq(hexa_map_get(p_peek_ahead(hexa_int(1)), "kind"), hexa_str("ColonColon")))))) {
        HexaVal ename = hexa_map_get(tok, "value");
        p_advance();
        p_advance();
        HexaVal vname = p_expect_ident();
        HexaVal data = hexa_str("");
        HexaVal pat_items = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LParen")))) {
            p_advance();
            HexaVal plist = hexa_array_new();
            while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RParen"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
                plist = hexa_array_push(plist, parse_match_pattern());
                if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
                    p_advance();
                }
            }
            p_expect(hexa_str("RParen"));
            if (hexa_truthy(hexa_eq(hexa_int(hexa_len(plist)), hexa_int(1)))) {
                data = hexa_index_get(plist, hexa_int(0));
            } else {
                pat_items = plist;
            }
        }
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("EnumPath")), "name", ename), "value", vname), "op", hexa_str("")), "left", data), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", pat_items), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    return parse_expr();
    return hexa_void();
}


HexaVal parse_block(void) {
    p_skip_newlines();
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal stmts = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        stmts = hexa_array_push(stmts, parse_stmt());
        p_skip_newlines();
    }
    p_expect(hexa_str("RBrace"));
    return stmts;
    return hexa_void();
}


HexaVal parse_expr(void) {
    return parse_null_coal();
    return hexa_void();
}


HexaVal parse_null_coal(void) {
    HexaVal left = parse_or();
    while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("NullCoal")))) {
        HexaVal op_tok = p_advance();
        HexaVal right = parse_or();
        left = hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("BinOp")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_map_get(op_tok, "value")), "left", left), "right", right), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    return left;
    return hexa_void();
}


HexaVal parse_or(void) {
    HexaVal left = parse_and();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Or"))) || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Xor")))))) {
        HexaVal op_tok = p_advance();
        HexaVal right = parse_and();
        left = hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("BinOp")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_map_get(op_tok, "value")), "left", left), "right", right), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    return left;
    return hexa_void();
}


HexaVal parse_and(void) {
    HexaVal left = parse_comparison();
    while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("And")))) {
        HexaVal op_tok = p_advance();
        HexaVal right = parse_comparison();
        left = hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("BinOp")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_map_get(op_tok, "value")), "left", left), "right", right), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    return left;
    return hexa_void();
}


HexaVal parse_comparison(void) {
    HexaVal left = parse_addition();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("EqEq"))) || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("NotEq"))))) || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Lt"))))) || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Gt"))))) || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LtEq"))))) || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("GtEq")))))) {
        HexaVal op_tok = p_advance();
        HexaVal right = parse_addition();
        left = hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("BinOp")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_map_get(op_tok, "value")), "left", left), "right", right), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    return left;
    return hexa_void();
}


/* T35+ (B11 fix): line-leading `+` continuation. Returns 1 if current tok
 * is Newline AND first non-newline ahead is `+` — advances past newlines
 * so caller's loop sees `+` next. Minus excluded (unary-ambiguous). */
static int _p_cont_plus(void) {
    if (!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Newline")))) return 0;
    long long o = 1;
    for (;;) {
        HexaVal t = p_peek_ahead(hexa_int(o));
        HexaVal tk = hexa_map_get(t, "kind");
        if (!hexa_truthy(hexa_eq(tk, hexa_str("Newline"))) && !hexa_truthy(hexa_eq(tk, hexa_str("Semicolon")))) {
            if (hexa_truthy(hexa_eq(tk, hexa_str("Plus")))) { p_skip_newlines(); return 1; }
            return 0;
        }
        o++;
    }
}

HexaVal parse_addition(void) {
    HexaVal left = parse_multiplication();
    /* rt-35 fix: accept bitwise &, |, ^ at additive precedence so
     * `x & y`, `x ^ y`, `x | y` parse as binary expressions. Ambiguity
     * with lambda `|...|` is avoided because lambdas start *expressions*,
     * not mid-expression. */
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Plus"))) || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Minus")))))
        || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("BitAnd")))
        || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("BitOr")))
        || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("BitXor")))
        || _p_cont_plus()) {
        HexaVal op_tok = p_advance();
        HexaVal right = parse_multiplication();
        left = hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("BinOp")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_map_get(op_tok, "value")), "left", left), "right", right), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    return left;
    return hexa_void();
}


HexaVal parse_multiplication(void) {
    HexaVal left = parse_unary();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Star"))) || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Slash"))))) || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Percent"))))) || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Power")))))) {
        HexaVal op_tok = p_advance();
        HexaVal right = parse_unary();
        left = hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("BinOp")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_map_get(op_tok, "value")), "left", left), "right", right), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    return left;
    return hexa_void();
}


HexaVal parse_unary(void) {
    HexaVal k = p_peek_kind();
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("Not"))) || hexa_truthy(hexa_eq(k, hexa_str("Minus"))))) || hexa_truthy(hexa_eq(k, hexa_str("BitNot")))))) {
        HexaVal op_tok = p_advance();
        HexaVal operand = parse_unary();
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("UnaryOp")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_map_get(op_tok, "value")), "left", operand), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    return parse_postfix();
    return hexa_void();
}


HexaVal parse_postfix(void) {
    HexaVal expr = parse_primary();
    HexaVal cont = hexa_bool(1);
    while (hexa_truthy(cont)) {
        HexaVal k = p_peek_kind();
        if (hexa_truthy(hexa_eq(k, hexa_str("LParen")))) {
            p_advance();
            HexaVal args = parse_args();
            p_expect(hexa_str("RParen"));
            expr = hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Call")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", args), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
        } else {
            if (hexa_truthy(hexa_eq(k, hexa_str("Dot")))) {
                p_advance();
                HexaVal field_name = p_expect_ident();
                expr = hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Field")), "name", field_name), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
            } else {
                if (hexa_truthy(hexa_eq(k, hexa_str("LBracket")))) {
                    p_advance();
                    HexaVal index = parse_expr();
                    p_expect(hexa_str("RBracket"));
                    expr = hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Index")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", index), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
                } else {
                    if (hexa_truthy(hexa_eq(k, hexa_str("DotDot")))) {
                        p_advance();
                        HexaVal end = parse_addition();
                        expr = hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Range")), "name", hexa_str("")), "value", hexa_str("exclusive")), "op", hexa_str("")), "left", expr), "right", end), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
                    } else {
                        if (hexa_truthy(hexa_eq(k, hexa_str("DotDotEq")))) {
                            p_advance();
                            HexaVal end = parse_addition();
                            expr = hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Range")), "name", hexa_str("")), "value", hexa_str("inclusive")), "op", hexa_str("")), "left", expr), "right", end), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
                        } else {
                            cont = hexa_bool(0);
                        }
                    }
                }
            }
        }
    }
    return expr;
    return hexa_void();
}


HexaVal parse_args(void) {
    HexaVal args = hexa_array_new();
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RParen")))) {
        return args;
    }
    args = hexa_array_push(args, parse_expr());
    while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
        p_advance();
        args = hexa_array_push(args, parse_expr());
    }
    return args;
    return hexa_void();
}


HexaVal parse_primary(void) {
    HexaVal tok = p_peek();
    HexaVal k = hexa_map_get(tok, "kind");
    if (hexa_truthy(hexa_eq(k, hexa_str("IntLit")))) {
        p_advance();
        return node_int_lit(hexa_map_get(tok, "value"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("FloatLit")))) {
        p_advance();
        return node_float_lit(hexa_map_get(tok, "value"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("BoolLit")))) {
        p_advance();
        return node_bool_lit(hexa_map_get(tok, "value"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("StringLit")))) {
        p_advance();
        return node_string_lit(hexa_map_get(tok, "value"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("CharLit")))) {
        p_advance();
        return node_char_lit(hexa_map_get(tok, "value"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Ident")))) {
        p_advance();
        HexaVal name = hexa_map_get(tok, "value");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("ColonColon")))) {
            p_advance();
            HexaVal variant = p_expect_ident();
            return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("EnumPath")), "name", name), "value", variant), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
        }
        HexaVal type_args = hexa_array_new();
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Lt")))) {
            HexaVal chars = hexa_str_chars(name);
            if (hexa_truthy(hexa_bool((hexa_int(hexa_len(chars))).i > (hexa_int(0)).i))) {
                HexaVal first_ch = hexa_index_get(chars, hexa_int(0));
                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((first_ch.tag==TAG_STR && first_ch.s && isalpha((unsigned char)first_ch.s[0])) || (first_ch.tag==TAG_CHAR && isalpha((unsigned char)first_ch.i)))) && hexa_truthy(hexa_eq(hexa_to_string(first_ch), hexa_str_to_upper(hexa_to_string(first_ch))))))) {
                    type_args = parse_type_params();
                }
            }
        }
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LBrace"))) && !p_no_struct_lit) {
            HexaVal chars = hexa_str_chars(name);
            if (hexa_truthy(hexa_bool((hexa_int(hexa_len(chars))).i > (hexa_int(0)).i))) {
                HexaVal first_ch = hexa_index_get(chars, hexa_int(0));
                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((first_ch.tag==TAG_STR && first_ch.s && isalpha((unsigned char)first_ch.s[0])) || (first_ch.tag==TAG_CHAR && isalpha((unsigned char)first_ch.i)))) && hexa_truthy(hexa_eq(hexa_to_string(first_ch), hexa_str_to_upper(hexa_to_string(first_ch))))))) {
                    p_advance();
                    p_skip_newlines();
                    HexaVal fields = hexa_array_new();
                    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
                        HexaVal fname = p_expect_ident();
                        p_expect(hexa_str("Colon"));
                        HexaVal fval = parse_expr();
                        fields = hexa_array_push(fields, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("FieldInit")), "name", fname), "value", hexa_str("")), "op", hexa_str("")), "left", fval), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
                        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
                            p_advance();
                        }
                        p_skip_newlines();
                    }
                    p_expect(hexa_str("RBrace"));
                    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("StructInit")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", fields), "items", type_args), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
                }
            }
        }
        return node_ident(name);
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("LParen")))) {
        p_advance();
        HexaVal expr = parse_expr();
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
            HexaVal items = hexa_array_push(hexa_array_new(), expr);
            while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
                p_advance();
                if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RParen")))) {
                } else {
                    items = hexa_array_push(items, parse_expr());
                }
            }
            p_expect(hexa_str("RParen"));
            return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Tuple")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", items), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
        }
        p_expect(hexa_str("RParen"));
        return expr;
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("LBracket")))) {
        p_advance();
        HexaVal items = hexa_array_new();
        while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBracket"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
            items = hexa_array_push(items, parse_expr());
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
                p_advance();
            }
        }
        p_expect(hexa_str("RBracket"));
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Array")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", items), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("HashLBrace")))) {
        p_advance();
        p_skip_newlines();
        HexaVal items = hexa_array_new();
        while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
            HexaVal key_expr = parse_expr();
            p_expect(hexa_str("Colon"));
            HexaVal val_expr = parse_expr();
            items = hexa_array_push(items, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("MapEntry")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", key_expr), "right", val_expr), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
                p_advance();
            }
            p_skip_newlines();
        }
        p_expect(hexa_str("RBrace"));
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("MapLit")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", items), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("If")))) {
        return parse_if_expr();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Match")))) {
        return parse_match_expr();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("BitOr")))) {
        p_advance();
        HexaVal lparams = hexa_array_new();
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("BitOr")))))) {
            lparams = hexa_array_push(lparams, p_expect_ident());
            while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
                p_advance();
                lparams = hexa_array_push(lparams, p_expect_ident());
            }
        }
        p_expect(hexa_str("BitOr"));
        HexaVal lbody = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LBrace")))) {
            lbody = parse_block();
        } else {
            lbody = parse_expr();
        }
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("Lambda")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", lbody), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", lparams), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    HexaVal msg3 = hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("unexpected token "), hexa_map_get(tok, "kind")), hexa_str(" ('")), hexa_map_get(tok, "value")), hexa_str("')"));
    p_record_error(hexa_map_get(tok, "line"), hexa_map_get(tok, "col"), msg3);
    hexa_println(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Parse error at "), hexa_to_string(hexa_map_get(tok, "line"))), hexa_str(":")), hexa_to_string(hexa_map_get(tok, "col"))), hexa_str(": ")), msg3));
    p_advance();
    return empty_node();
    return hexa_void();
}


HexaVal parse_try_catch(void) {
    p_advance();
    HexaVal try_body = parse_block();
    p_skip_newlines();
    p_expect(hexa_str("Catch"));
    HexaVal catch_var = hexa_str("_");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LParen")))) {
        p_advance();
        catch_var = p_expect_ident();
        p_expect(hexa_str("RParen"));
    } else {
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Ident")))) {
            catch_var = p_expect_ident();
        }
    }
    HexaVal catch_body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("TryCatch")), "name", catch_var), "value", hexa_str("")), "op", hexa_str("")), "left", try_body), "right", catch_body), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_throw_stmt(void) {
    p_advance();
    HexaVal expr = parse_expr();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ThrowStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_panic_stmt(void) {
    p_advance();
    p_expect(hexa_str("LParen"));
    HexaVal msg = parse_expr();
    p_expect(hexa_str("RParen"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("PanicStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", msg), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_recover_stmt(void) {
    p_advance();
    HexaVal catch_var = hexa_str("_");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("BitOr")))) {
        p_advance();
        catch_var = p_expect_ident();
        p_expect(hexa_str("BitOr"));
    }
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("RecoverStmt")), "name", catch_var), "value", hexa_str("")), "op", hexa_str("")), "left", body), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_drop_stmt(void) {
    p_advance();
    p_expect(hexa_str("LParen"));
    HexaVal var_name = p_expect_ident();
    p_expect(hexa_str("RParen"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("DropStmt")), "name", var_name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_spawn_stmt(void) {
    p_advance();
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("SpawnStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_scope_stmt(void) {
    p_advance();
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ScopeStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_select_stmt(void) {
    p_advance();
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal arms = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        HexaVal expr = parse_expr();
        p_expect(hexa_str("Arrow"));
        HexaVal body = parse_block();
        arms = hexa_array_push(arms, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("SelectArm")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
        p_skip_newlines();
    }
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("SelectStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", arms), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_async_fn(void) {
    p_advance();
    p_expect(hexa_str("Fn"));
    HexaVal name = p_expect_ident();
    p_expect(hexa_str("LParen"));
    HexaVal params = parse_params();
    p_expect(hexa_str("RParen"));
    HexaVal ret = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Arrow")))) {
        p_advance();
        ret = p_expect_ident();
    }
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("AsyncFnDecl")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", params), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", ret), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_attribute_prefix(void) {
    HexaVal attr = p_advance();
    HexaVal attr_name = hexa_map_get(attr, "value");
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(attr_name, hexa_str("symbol"))) || hexa_truthy(hexa_eq(attr_name, hexa_str("link")))))) {
        p_expect(hexa_str("LParen"));
        HexaVal tok = p_advance();
        HexaVal val = hexa_map_get(tok, "value");
        p_expect(hexa_str("RParen"));
        if (hexa_truthy(hexa_eq(attr_name, hexa_str("symbol")))) {
            p_pending_symbol = val;
        } else {
            p_pending_link = val;
        }
        p_skip_newlines();
        return parse_stmt();
    }
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LParen")))) {
        p_advance();
        HexaVal depth = hexa_int(1);
        while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((depth).i > (hexa_int(0)).i)) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LParen")))) {
                depth = hexa_add(depth, hexa_int(1));
            }
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RParen")))) {
                depth = hexa_int((depth).i - (hexa_int(1)).i);
            }
            p_advance();
        }
    }
    p_skip_newlines();
    return parse_stmt();
    return hexa_void();
}


HexaVal parse_extern_fn(void) {
    p_advance();
    p_expect(hexa_str("Fn"));
    HexaVal name = p_expect_ident();
    p_expect(hexa_str("LParen"));
    HexaVal params = parse_params();
    p_expect(hexa_str("RParen"));
    HexaVal ret = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Arrow")))) {
        p_advance();
        ret = parse_type_annotation();
    }
    HexaVal body = hexa_array_new();
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LBrace")))) {
        body = parse_block();
    }
    HexaVal sym = p_pending_symbol;
    HexaVal link = p_pending_link;
    p_pending_symbol = hexa_str("");
    p_pending_link = hexa_str("");
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ExternFnDecl")), "name", name), "value", sym), "op", link), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", params), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", ret), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_type_alias(void) {
    p_advance();
    HexaVal name = p_expect_ident();
    p_expect(hexa_str("Eq"));
    HexaVal typ = p_expect_ident();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("TypeAlias")), "name", name), "value", typ), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_atomic_let(void) {
    p_advance();
    p_expect(hexa_str("Let"));
    HexaVal name = p_expect_ident();
    p_expect(hexa_str("Eq"));
    HexaVal expr = parse_expr();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("AtomicLet")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_break_stmt(void) {
    p_advance();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("BreakStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_continue_stmt(void) {
    p_advance();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ContinueStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_yield_stmt(void) {
    p_advance();
    HexaVal expr = hexa_str("");
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Newline"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        expr = parse_expr();
    }
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("YieldStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_comptime_stmt(void) {
    p_advance();
    HexaVal next = p_peek_kind();
    if (hexa_truthy(hexa_eq(next, hexa_str("Fn")))) {
        p_advance();
        HexaVal name = p_expect_ident();
        p_expect(hexa_str("LParen"));
        HexaVal params = parse_params();
        p_expect(hexa_str("RParen"));
        HexaVal ret = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Arrow")))) {
            p_advance();
            ret = p_expect_ident();
        }
        HexaVal body = parse_block();
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ComptimeFnDecl")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", params), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", ret), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    if (hexa_truthy(hexa_eq(next, hexa_str("Const")))) {
        p_advance();
        HexaVal name = p_expect_ident();
        HexaVal typ = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Colon")))) {
            p_advance();
            typ = p_expect_ident();
        }
        p_expect(hexa_str("Eq"));
        HexaVal expr = parse_expr();
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ComptimeConst")), "name", name), "value", typ), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    if (hexa_truthy(hexa_eq(next, hexa_str("Let")))) {
        p_advance();
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Mut")))) {
            p_advance();
        }
        HexaVal name = p_expect_ident();
        HexaVal typ = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Colon")))) {
            p_advance();
            typ = p_expect_ident();
        }
        p_expect(hexa_str("Eq"));
        HexaVal expr = parse_expr();
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ComptimeLet")), "name", name), "value", typ), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    if (hexa_truthy(hexa_eq(next, hexa_str("Assert")))) {
        p_advance();
        HexaVal expr = parse_expr();
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ComptimeAssert")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", expr), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("ComptimeBlock")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_intent_stmt(void) {
    p_advance();
    HexaVal desc = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("StringLit")))) {
        desc = hexa_map_get(p_advance(), "value");
    } else {
        desc = p_expect_ident();
    }
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal fields = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        HexaVal key = p_expect_ident();
        p_expect(hexa_str("Colon"));
        HexaVal val = parse_expr();
        fields = hexa_array_push(fields, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("IntentField")), "name", key), "value", hexa_str("")), "op", hexa_str("")), "left", val), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
            p_advance();
        }
        p_skip_newlines();
    }
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("IntentStmt")), "name", desc), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", fields), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_verify_stmt(void) {
    p_advance();
    HexaVal name = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("StringLit")))) {
        name = hexa_map_get(p_advance(), "value");
    } else {
        name = p_expect_ident();
    }
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("VerifyStmt")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_generate_stmt(void) {
    p_advance();
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Fn")))) {
        p_advance();
        HexaVal name = p_expect_ident();
        p_expect(hexa_str("LParen"));
        HexaVal params = parse_params();
        p_expect(hexa_str("RParen"));
        HexaVal ret = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Arrow")))) {
            p_advance();
            ret = p_expect_ident();
        }
        p_expect(hexa_str("LBrace"));
        p_skip_newlines();
        HexaVal desc = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("StringLit")))) {
            desc = hexa_map_get(p_advance(), "value");
        }
        p_skip_newlines();
        p_expect(hexa_str("RBrace"));
        return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("GenerateFnStmt")), "name", name), "value", desc), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", params), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", ret), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    }
    HexaVal type_hint = hexa_str("");
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LBrace")))))) {
        type_hint = p_expect_ident();
    }
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal desc = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("StringLit")))) {
        desc = hexa_map_get(p_advance(), "value");
    }
    p_skip_newlines();
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("GenerateStmt")), "name", type_hint), "value", desc), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_optimize_stmt(void) {
    p_advance();
    p_expect(hexa_str("Fn"));
    HexaVal name = p_expect_ident();
    p_expect(hexa_str("LParen"));
    HexaVal params = parse_params();
    p_expect(hexa_str("RParen"));
    HexaVal ret = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Arrow")))) {
        p_advance();
        ret = p_expect_ident();
    }
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("OptimizeFnStmt")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", params), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", ret), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_effect_decl(void) {
    p_advance();
    HexaVal name = p_expect_ident();
    HexaVal tparams = hexa_array_new();
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Lt")))) {
        tparams = parse_type_params();
    }
    p_skip_newlines();
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal ops = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        p_expect(hexa_str("Fn"));
        HexaVal op_name = p_expect_ident();
        p_expect(hexa_str("LParen"));
        HexaVal op_params = parse_params();
        p_expect(hexa_str("RParen"));
        HexaVal op_ret = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Arrow")))) {
            p_advance();
            op_ret = p_expect_ident();
        }
        ops = hexa_array_push(ops, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("EffectOp")), "name", op_name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", op_params), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", op_ret), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
        p_skip_newlines();
    }
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("EffectDecl")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", tparams), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", ops), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_pure_fn(void) {
    p_advance();
    p_expect(hexa_str("Fn"));
    HexaVal name = p_expect_ident();
    p_expect(hexa_str("LParen"));
    HexaVal params = parse_params();
    p_expect(hexa_str("RParen"));
    HexaVal ret = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Arrow")))) {
        p_advance();
        ret = p_expect_ident();
    }
    HexaVal body = parse_block();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("PureFnDecl")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", params), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", ret), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_macro_def(void) {
    p_advance();
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Not")))) {
        p_advance();
    }
    HexaVal name = p_expect_ident();
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal rules = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        p_expect(hexa_str("LParen"));
        p_skip_newlines();
        HexaVal pat_tokens = hexa_array_new();
        while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RParen"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
            HexaVal tok = p_advance();
            pat_tokens = hexa_array_push(pat_tokens, hexa_map_get(tok, "value"));
            p_skip_newlines();
        }
        p_expect(hexa_str("RParen"));
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eq")))) {
            p_advance();
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Gt")))) {
                p_advance();
            }
        }
        HexaVal body = parse_block();
        rules = hexa_array_push(rules, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("MacroRule")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", pat_tokens), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma"))) || hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Semicolon")))))) {
            p_advance();
        }
        p_skip_newlines();
    }
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("MacroDef")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", rules), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_derive_decl(void) {
    p_advance();
    p_expect(hexa_str("LParen"));
    HexaVal traits = hexa_array_new();
    traits = hexa_array_push(traits, p_expect_ident());
    while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
        p_advance();
        traits = hexa_array_push(traits, p_expect_ident());
    }
    p_expect(hexa_str("RParen"));
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("For")))) {
        p_advance();
    } else {
        p_expect_ident();
    }
    HexaVal type_name = p_expect_ident();
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("DeriveDecl")), "name", type_name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", traits), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_theorem_stmt(void) {
    p_advance();
    HexaVal name = p_expect_ident();
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal stmts = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        stmts = hexa_array_push(stmts, parse_stmt());
        p_skip_newlines();
    }
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("TheoremStmt")), "name", name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", stmts), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_handle_stmt(void) {
    p_advance();
    HexaVal body = parse_block();
    p_skip_newlines();
    p_expect_ident();
    p_skip_newlines();
    p_expect(hexa_str("LBrace"));
    p_skip_newlines();
    HexaVal handlers = hexa_array_new();
    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RBrace"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
        HexaVal effect_name = p_expect_ident();
        p_expect(hexa_str("Dot"));
        HexaVal op_name = p_expect_ident();
        HexaVal hparams = hexa_array_new();
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LParen")))) {
            p_advance();
            while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("RParen"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eof")))))))) {
                hparams = hexa_array_push(hparams, p_expect_ident());
                if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
                    p_advance();
                }
            }
            p_expect(hexa_str("RParen"));
        }
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Eq")))) {
            p_advance();
            if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Gt")))) {
                p_advance();
            }
        }
        HexaVal hbody = parse_block();
        handlers = hexa_array_push(handlers, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("EffectHandler")), "name", op_name), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hparams), "body", hbody), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", effect_name), "trait_name", hexa_str("")), "methods", hexa_str("")));
        p_skip_newlines();
    }
    p_expect(hexa_str("RBrace"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("HandleWithStmt")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", body), "args", hexa_str("")), "fields", hexa_str("")), "items", handlers), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal parse_visibility(void) {
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Pub")))) {
        p_advance();
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("LParen")))) {
            p_advance();
            HexaVal vis_scope = p_expect_ident();
            p_expect(hexa_str("RParen"));
            return vis_scope;
        }
        return hexa_str("pub");
    }
    return hexa_str("private");
    return hexa_void();
}


HexaVal parse_type_params(void) {
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Lt")))))) {
        return hexa_array_new();
    }
    p_advance();
    HexaVal tparams = hexa_array_new();
    HexaVal name = p_expect_ident();
    HexaVal bound = hexa_str("");
    if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Colon")))) {
        p_advance();
        bound = p_expect_ident();
    }
    tparams = hexa_array_push(tparams, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("TypeParam")), "name", name), "value", bound), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
    while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
        p_advance();
        HexaVal pname = p_expect_ident();
        HexaVal pbound = hexa_str("");
        if (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Colon")))) {
            p_advance();
            pbound = p_expect_ident();
        }
        tparams = hexa_array_push(tparams, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("TypeParam")), "name", pname), "value", pbound), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
    }
    p_expect(hexa_str("Gt"));
    return tparams;
    return hexa_void();
}


HexaVal parse_where_clauses(void) {
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Where")))))) {
        return hexa_array_new();
    }
    p_advance();
    HexaVal clauses = hexa_array_new();
    HexaVal tname = p_expect_ident();
    p_expect(hexa_str("Colon"));
    HexaVal tbound = p_expect_ident();
    clauses = hexa_array_push(clauses, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("WhereClause")), "name", tname), "value", tbound), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
    while (hexa_truthy(hexa_eq(p_peek_kind(), hexa_str("Comma")))) {
        p_advance();
        HexaVal wname = p_expect_ident();
        p_expect(hexa_str("Colon"));
        HexaVal wbound = p_expect_ident();
        clauses = hexa_array_push(clauses, hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", hexa_str("WhereClause")), "name", wname), "value", wbound), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str("")));
    }
    return clauses;
    return hexa_void();
}


HexaVal ast_to_string(HexaVal node, HexaVal indent) {
    HexaVal pad = hexa_str("");
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (indent).i))) {
        pad = hexa_add(pad, hexa_str("  "));
        i = hexa_add(i, hexa_int(1));
    }
    HexaVal k = hexa_map_get(node, "kind");
    if (hexa_truthy(hexa_eq(k, hexa_str("IntLit")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("IntLit(")), hexa_map_get(node, "value")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("FloatLit")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("FloatLit(")), hexa_map_get(node, "value")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("BoolLit")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("BoolLit(")), hexa_map_get(node, "value")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("StringLit")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("StringLit(\"")), hexa_map_get(node, "value")), hexa_str("\")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("CharLit")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("CharLit('")), hexa_map_get(node, "value")), hexa_str("')"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Ident")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("Ident(")), hexa_map_get(node, "name")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Wildcard")))) {
        return hexa_add(pad, hexa_str("Wildcard"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("BinOp")))) {
        HexaVal l = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        HexaVal r = ast_to_string(hexa_map_get(node, "right"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("BinOp(")), hexa_map_get(node, "op")), hexa_str(", ")), l), hexa_str(", ")), r), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("UnaryOp")))) {
        HexaVal operand = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("UnaryOp(")), hexa_map_get(node, "op")), hexa_str(", ")), operand), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Call")))) {
        HexaVal callee = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("Call(")), callee), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "args"))))), hexa_str(" args])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Field")))) {
        HexaVal obj = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("Field(")), obj), hexa_str(".")), hexa_map_get(node, "name")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Index")))) {
        HexaVal obj = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        HexaVal idx = ast_to_string(hexa_map_get(node, "right"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("Index(")), obj), hexa_str("[")), idx), hexa_str("])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("IfExpr")))) {
        HexaVal c = ast_to_string(hexa_map_get(node, "cond"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("IfExpr(")), c), hexa_str(", then=[")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "then_body"))))), hexa_str("], else=[")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "else_body"))))), hexa_str("])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("MatchExpr")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("MatchExpr([")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "arms"))))), hexa_str(" arms])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Array")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("Array([")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "items"))))), hexa_str(" items])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("StructInit")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("StructInit(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "fields"))))), hexa_str(" fields])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Range")))) {
        HexaVal s = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        HexaVal e = ast_to_string(hexa_map_get(node, "right"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("Range(")), s), hexa_str("..")), e), hexa_str(", ")), hexa_map_get(node, "value")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("EnumPath")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("EnumPath(")), hexa_map_get(node, "name")), hexa_str("::")), hexa_map_get(node, "value")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Lambda")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("Lambda([")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "params"))))), hexa_str(" params])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Tuple")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("Tuple([")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "items"))))), hexa_str(" items])"));
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("LetStmt"))) || hexa_truthy(hexa_eq(k, hexa_str("LetMutStmt")))))) {
        HexaVal v = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, k), hexa_str("(")), hexa_map_get(node, "name")), hexa_str(" = ")), v), hexa_str(")"));
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("ConstStmt"))) || hexa_truthy(hexa_eq(k, hexa_str("StaticStmt")))))) {
        HexaVal v = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, k), hexa_str("(")), hexa_map_get(node, "name")), hexa_str(" = ")), v), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ReturnStmt")))) {
        if (hexa_truthy(hexa_eq(hexa_map_get(node, "left"), hexa_str("")))) {
            return hexa_add(pad, hexa_str("ReturnStmt()"));
        }
        HexaVal v = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("ReturnStmt(")), v), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("FnDecl")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("FnDecl(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "params"))))), hexa_str(" params], [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "body"))))), hexa_str(" stmts])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("StructDecl")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("StructDecl(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "fields"))))), hexa_str(" fields])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("EnumDecl")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("EnumDecl(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "variants"))))), hexa_str(" variants])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ForStmt")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("ForStmt(")), hexa_map_get(node, "name")), hexa_str(" in ..., [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "body"))))), hexa_str(" stmts])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("WhileStmt")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("WhileStmt(..., [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "body"))))), hexa_str(" stmts])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("LoopStmt")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("LoopStmt([")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "body"))))), hexa_str(" stmts])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ExprStmt")))) {
        HexaVal v = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("ExprStmt(")), v), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("AssignStmt")))) {
        HexaVal l = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        HexaVal r = ast_to_string(hexa_map_get(node, "right"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("AssignStmt(")), l), hexa_str(" = ")), r), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("AssertStmt")))) {
        HexaVal v = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("AssertStmt(")), v), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ProofStmt")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("ProofStmt(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "body"))))), hexa_str(" stmts])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ImplBlock")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("ImplBlock(")), hexa_map_get(node, "target")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "methods"))))), hexa_str(" methods])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("TraitDecl")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("TraitDecl(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "methods"))))), hexa_str(" methods])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("UseStmt")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("UseStmt([")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "items"))))), hexa_str(" segments])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ModStmt")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("ModStmt(")), hexa_map_get(node, "name")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("BreakStmt")))) {
        return hexa_add(pad, hexa_str("BreakStmt"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ContinueStmt")))) {
        return hexa_add(pad, hexa_str("ContinueStmt"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("YieldStmt")))) {
        if (hexa_truthy(hexa_eq(hexa_map_get(node, "left"), hexa_str("")))) {
            return hexa_add(pad, hexa_str("YieldStmt()"));
        }
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("YieldStmt(")), ast_to_string(hexa_map_get(node, "left"), hexa_int(0))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ComptimeFnDecl")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("ComptimeFnDecl(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "params"))))), hexa_str(" params])"));
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("ComptimeConst"))) || hexa_truthy(hexa_eq(k, hexa_str("ComptimeLet")))))) {
        HexaVal v = ast_to_string(hexa_map_get(node, "left"), hexa_int(0));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, k), hexa_str("(")), hexa_map_get(node, "name")), hexa_str(" = ")), v), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ComptimeBlock")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("ComptimeBlock([")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "body"))))), hexa_str(" stmts])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ComptimeAssert")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("ComptimeAssert(")), ast_to_string(hexa_map_get(node, "left"), hexa_int(0))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("IntentStmt")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("IntentStmt(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "fields"))))), hexa_str(" fields])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("VerifyStmt")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("VerifyStmt(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "body"))))), hexa_str(" stmts])"));
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("GenerateFnStmt"))) || hexa_truthy(hexa_eq(k, hexa_str("GenerateStmt")))))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(pad, k), hexa_str("(")), hexa_map_get(node, "name")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("OptimizeFnStmt")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("OptimizeFnStmt(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "params"))))), hexa_str(" params])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("EffectDecl")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("EffectDecl(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "items"))))), hexa_str(" ops])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("PureFnDecl")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("PureFnDecl(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "params"))))), hexa_str(" params])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("MacroDef")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("MacroDef(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "items"))))), hexa_str(" rules])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("DeriveDecl")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("DeriveDecl(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "items"))))), hexa_str(" traits])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("TheoremStmt")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("TheoremStmt(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "body"))))), hexa_str(" stmts])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("HandleWithStmt")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("HandleWithStmt([")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "items"))))), hexa_str(" handlers])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("TryCatch")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("TryCatch(")), hexa_map_get(node, "name")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ThrowStmt")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("ThrowStmt(")), ast_to_string(hexa_map_get(node, "left"), hexa_int(0))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("PanicStmt")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("PanicStmt(")), ast_to_string(hexa_map_get(node, "left"), hexa_int(0))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("DropStmt")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("DropStmt(")), hexa_map_get(node, "name")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("SpawnStmt")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("SpawnStmt([")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "body"))))), hexa_str(" stmts])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ScopeStmt")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("ScopeStmt([")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "body"))))), hexa_str(" stmts])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("SelectStmt")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("SelectStmt([")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "arms"))))), hexa_str(" arms])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("AsyncFnDecl")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("AsyncFnDecl(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "params"))))), hexa_str(" params])"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ExternFnDecl")))) {
        HexaVal sym_info = hexa_str("");
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_map_get(node, "value"), hexa_str("")))))) {
            sym_info = hexa_add(hexa_add(hexa_str(", @symbol(\""), hexa_map_get(node, "value")), hexa_str("\")"));
        }
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("ExternFnDecl(")), hexa_map_get(node, "name")), hexa_str(", [")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "params"))))), hexa_str(" params]")), sym_info), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("TypeAlias")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("TypeAlias(")), hexa_map_get(node, "name")), hexa_str(" = ")), hexa_map_get(node, "value")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("AtomicLet")))) {
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("AtomicLet(")), hexa_map_get(node, "name")), hexa_str(")"));
    }
    return hexa_add(hexa_add(hexa_add(pad, hexa_str("Unknown(")), k), hexa_str(")"));
    return hexa_void();
}


HexaVal print_ast(HexaVal stmts) {
    {
        HexaVal __iter_arr = stmts;
        for (int __fi = 0; __fi < hexa_len(__iter_arr); __fi++) {
            HexaVal s = hexa_array_get(__iter_arr, __fi);
            hexa_println(ast_to_string(s, hexa_int(0)));
        }
    }
    return hexa_void();
}


HexaVal is_keyword(HexaVal word) {
    if (hexa_truthy(hexa_eq(word, hexa_str("if")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("else")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("match")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("for")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("while")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("loop")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("type")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("struct")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("enum")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("trait")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("impl")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("fn")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("return")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("yield")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("async")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("await")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("let")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("mut")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("const")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("static")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("mod")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("use")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("pub")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("crate")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("own")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("borrow")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("move")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("drop")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("spawn")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("channel")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("select")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("atomic")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("effect")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("handle")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("resume")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("pure")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("proof")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("assert")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("invariant")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("theorem")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("macro")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("derive")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("where")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("comptime")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("try")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("catch")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("throw")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("panic")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("recover")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("intent")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("generate")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("verify")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("optimize")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("break")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("continue")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("extern")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("dyn")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("scope")))) {
        return hexa_bool(1);
    }
    return hexa_bool(0);
    return hexa_void();
}


HexaVal keyword_kind(HexaVal word) {
    if (hexa_truthy(hexa_eq(word, hexa_str("true")))) {
        return hexa_str("BoolLit");
    }
    if (hexa_truthy(hexa_eq(word, hexa_str("false")))) {
        return hexa_str("BoolLit");
    }
    if (hexa_truthy(is_keyword(word))) {
        HexaVal first = hexa_str_to_upper(hexa_to_string(hexa_index_get(hexa_str_chars(word), hexa_int(0))));
        HexaVal rest = hexa_str("");
        HexaVal chars = hexa_str_chars(word);
        HexaVal i = hexa_int(1);
        while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(chars))).i))) {
            rest = hexa_add(rest, hexa_to_string(hexa_index_get(chars, i)));
            i = hexa_add(i, hexa_int(1));
        }
        return hexa_add(first, rest);
    }
    return hexa_str("Ident");
    return hexa_void();
}


HexaVal is_ident_start(HexaVal ch) {
    if (hexa_truthy(hexa_bool((ch.tag==TAG_STR && ch.s && isalpha((unsigned char)ch.s[0])) || (ch.tag==TAG_CHAR && isalpha((unsigned char)ch.i))))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(ch, hexa_str("_")))) {
        return hexa_bool(1);
    }
    return hexa_bool(0);
    return hexa_void();
}


HexaVal is_ident_char(HexaVal ch) {
    if (hexa_truthy(hexa_bool((ch.tag==TAG_STR && ch.s && isalnum((unsigned char)ch.s[0])) || (ch.tag==TAG_CHAR && isalnum((unsigned char)ch.i))))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(ch, hexa_str("_")))) {
        return hexa_bool(1);
    }
    return hexa_bool(0);
    return hexa_void();
}


HexaVal tokenize(HexaVal source) {
    HexaVal tokens = hexa_array_new();
    HexaVal chars = hexa_str_chars(source);
    HexaVal pos = hexa_int(0);
    HexaVal line = hexa_int(1);
    HexaVal col = hexa_int(1);
    HexaVal total = hexa_int(hexa_len(chars));
    while (hexa_truthy(hexa_bool((pos).i < (total).i))) {
        HexaVal ch = hexa_index_get(chars, pos);
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ch, hexa_str(" "))) || hexa_truthy(hexa_eq(ch, hexa_str("\t"))))) || hexa_truthy(hexa_eq(ch, hexa_str("\r")))))) {
            pos = hexa_add(pos, hexa_int(1));
            col = hexa_add(col, hexa_int(1));
        } else {
            if (hexa_truthy(hexa_eq(ch, hexa_str("\n")))) {
                tokens = hexa_array_push(tokens, Token(hexa_str("Newline"), hexa_str("\n"), line, col));
                pos = hexa_add(pos, hexa_int(1));
                line = hexa_add(line, hexa_int(1));
                col = hexa_int(1);
            } else {
                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ch, hexa_str("/"))) && hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)))) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("/")))))) {
                    pos = hexa_add(pos, hexa_int(2));
                    col = hexa_add(col, hexa_int(2));
                    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((pos).i < (total).i)) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_index_get(chars, pos), hexa_str("\n")))))))) {
                        pos = hexa_add(pos, hexa_int(1));
                        col = hexa_add(col, hexa_int(1));
                    }
                } else {
                    if (hexa_truthy(is_ident_start(ch))) {
                        HexaVal start_col = col;
                        HexaVal word = hexa_str("");
                        while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((pos).i < (total).i)) && hexa_truthy(is_ident_char(hexa_index_get(chars, pos)))))) {
                            word = hexa_add(word, hexa_to_string(hexa_index_get(chars, pos)));
                            pos = hexa_add(pos, hexa_int(1));
                            col = hexa_add(col, hexa_int(1));
                        }
                        HexaVal kind = keyword_kind(word);
                        tokens = hexa_array_push(tokens, Token(kind, word, line, start_col));
                    } else {
                        if (hexa_truthy(hexa_bool((ch.tag==TAG_STR && ch.s && isdigit((unsigned char)ch.s[0])) || (ch.tag==TAG_CHAR && isdigit((unsigned char)ch.i))))) {
                            HexaVal start_col = col;
                            HexaVal num_str = hexa_str("");
                            HexaVal is_float = hexa_bool(0);
                            while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((pos).i < (total).i)) && hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_index_get(chars, pos).tag==TAG_STR && hexa_index_get(chars, pos).s && isdigit((unsigned char)hexa_index_get(chars, pos).s[0])) || (hexa_index_get(chars, pos).tag==TAG_CHAR && isdigit((unsigned char)hexa_index_get(chars, pos).i)))) || hexa_truthy(hexa_eq(hexa_index_get(chars, pos), hexa_str("_")))))))) {
                                if (hexa_truthy(hexa_eq(hexa_index_get(chars, pos), hexa_str("_")))) {
                                    pos = hexa_add(pos, hexa_int(1));
                                    col = hexa_add(col, hexa_int(1));
                                } else {
                                    num_str = hexa_add(num_str, hexa_to_string(hexa_index_get(chars, pos)));
                                    pos = hexa_add(pos, hexa_int(1));
                                    col = hexa_add(col, hexa_int(1));
                                }
                            }
                            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((pos).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, pos), hexa_str(".")))))) {
                                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_bool((hexa_index_get(chars, hexa_add(pos, hexa_int(1))).tag==TAG_STR && hexa_index_get(chars, hexa_add(pos, hexa_int(1))).s && isdigit((unsigned char)hexa_index_get(chars, hexa_add(pos, hexa_int(1))).s[0])) || (hexa_index_get(chars, hexa_add(pos, hexa_int(1))).tag==TAG_CHAR && isdigit((unsigned char)hexa_index_get(chars, hexa_add(pos, hexa_int(1))).i))))))) {
                                    is_float = hexa_bool(1);
                                    num_str = hexa_add(num_str, hexa_str("."));
                                    pos = hexa_add(pos, hexa_int(1));
                                    col = hexa_add(col, hexa_int(1));
                                    while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((pos).i < (total).i)) && hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_index_get(chars, pos).tag==TAG_STR && hexa_index_get(chars, pos).s && isdigit((unsigned char)hexa_index_get(chars, pos).s[0])) || (hexa_index_get(chars, pos).tag==TAG_CHAR && isdigit((unsigned char)hexa_index_get(chars, pos).i)))) || hexa_truthy(hexa_eq(hexa_index_get(chars, pos), hexa_str("_")))))))) {
                                        if (hexa_truthy(hexa_eq(hexa_index_get(chars, pos), hexa_str("_")))) {
                                            pos = hexa_add(pos, hexa_int(1));
                                            col = hexa_add(col, hexa_int(1));
                                        } else {
                                            num_str = hexa_add(num_str, hexa_to_string(hexa_index_get(chars, pos)));
                                            pos = hexa_add(pos, hexa_int(1));
                                            col = hexa_add(col, hexa_int(1));
                                        }
                                    }
                                }
                            }
                            /* rt-35 fix: scientific notation `1e-10`, `1.0E+5`, `2e8`.
                             * After optional fractional part, accept e|E, optional +|-, digits.
                             * Force FloatLit when exponent present. */
                            if ((pos).i < (total).i) {
                                HexaVal __ech = hexa_index_get(chars, pos);
                                if (hexa_truthy(hexa_eq(__ech, hexa_str("e"))) || hexa_truthy(hexa_eq(__ech, hexa_str("E")))) {
                                    HexaVal __save_pos = pos;
                                    HexaVal __save_col = col;
                                    HexaVal __exp_str = hexa_str("e");
                                    pos = hexa_add(pos, hexa_int(1));
                                    col = hexa_add(col, hexa_int(1));
                                    if ((pos).i < (total).i) {
                                        HexaVal __sch = hexa_index_get(chars, pos);
                                        if (hexa_truthy(hexa_eq(__sch, hexa_str("+"))) || hexa_truthy(hexa_eq(__sch, hexa_str("-")))) {
                                            __exp_str = hexa_add(__exp_str, hexa_to_string(__sch));
                                            pos = hexa_add(pos, hexa_int(1));
                                            col = hexa_add(col, hexa_int(1));
                                        }
                                    }
                                    int __have_digit = 0;
                                    while ((pos).i < (total).i) {
                                        HexaVal __dch = hexa_index_get(chars, pos);
                                        if ((__dch.tag==TAG_STR && __dch.s && isdigit((unsigned char)__dch.s[0])) || (__dch.tag==TAG_CHAR && isdigit((unsigned char)__dch.i))) {
                                            __exp_str = hexa_add(__exp_str, hexa_to_string(__dch));
                                            pos = hexa_add(pos, hexa_int(1));
                                            col = hexa_add(col, hexa_int(1));
                                            __have_digit = 1;
                                        } else {
                                            break;
                                        }
                                    }
                                    if (__have_digit) {
                                        num_str = hexa_add(num_str, __exp_str);
                                        is_float = hexa_bool(1);
                                    } else {
                                        pos = __save_pos;
                                        col = __save_col;
                                    }
                                }
                            }
                            if (hexa_truthy(is_float)) {
                                tokens = hexa_array_push(tokens, Token(hexa_str("FloatLit"), num_str, line, start_col));
                            } else {
                                tokens = hexa_array_push(tokens, Token(hexa_str("IntLit"), num_str, line, start_col));
                            }
                        } else {
                            if (hexa_truthy(hexa_eq(ch, hexa_str("\"")))) {
                                HexaVal start_col = col;
                                pos = hexa_add(pos, hexa_int(1));
                                col = hexa_add(col, hexa_int(1));
                                HexaVal s = hexa_str("");
                                HexaVal done = hexa_bool(0);
                                while (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((pos).i < (total).i)) && hexa_truthy(hexa_bool(!hexa_truthy(done)))))) {
                                    HexaVal c = hexa_index_get(chars, pos);
                                    if (hexa_truthy(hexa_eq(c, hexa_str("\"")))) {
                                        done = hexa_bool(1);
                                        pos = hexa_add(pos, hexa_int(1));
                                        col = hexa_add(col, hexa_int(1));
                                    } else {
                                        if (hexa_truthy(hexa_eq(c, hexa_str("\\")))) {
                                            pos = hexa_add(pos, hexa_int(1));
                                            col = hexa_add(col, hexa_int(1));
                                            if (hexa_truthy(hexa_bool((pos).i < (total).i))) {
                                                HexaVal esc = hexa_index_get(chars, pos);
                                                if (hexa_truthy(hexa_eq(esc, hexa_str("n")))) {
                                                    s = hexa_add(s, hexa_str("\n"));
                                                } else {
                                                    if (hexa_truthy(hexa_eq(esc, hexa_str("t")))) {
                                                        s = hexa_add(s, hexa_str("\t"));
                                                    } else {
                                                        if (hexa_truthy(hexa_eq(esc, hexa_str("r")))) {
                                                            s = hexa_add(s, hexa_str("\r"));
                                                        } else {
                                                            if (hexa_truthy(hexa_eq(esc, hexa_str("\\")))) {
                                                                s = hexa_add(s, hexa_str("\\"));
                                                            } else {
                                                                if (hexa_truthy(hexa_eq(esc, hexa_str("\"")))) {
                                                                    s = hexa_add(s, hexa_str("\""));
                                                                } else {
                                                                    if (hexa_truthy(hexa_eq(esc, hexa_str("0")))) {
                                                                        s = hexa_add(s, hexa_str(" "));
                                                                    } else {
                                                                        s = hexa_add(s, hexa_to_string(esc));
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                                pos = hexa_add(pos, hexa_int(1));
                                                col = hexa_add(col, hexa_int(1));
                                            }
                                        } else {
                                            s = hexa_add(s, hexa_to_string(c));
                                            pos = hexa_add(pos, hexa_int(1));
                                            if (hexa_truthy(hexa_eq(c, hexa_str("\n")))) {
                                                line = hexa_add(line, hexa_int(1));
                                                col = hexa_int(1);
                                            } else {
                                                col = hexa_add(col, hexa_int(1));
                                            }
                                        }
                                    }
                                }
                                tokens = hexa_array_push(tokens, Token(hexa_str("StringLit"), s, line, start_col));
                            } else {
                                if (hexa_truthy(hexa_eq(ch, hexa_str("'")))) {
                                    HexaVal start_col = col;
                                    pos = hexa_add(pos, hexa_int(1));
                                    col = hexa_add(col, hexa_int(1));
                                    HexaVal ch_val = hexa_str("");
                                    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((pos).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, pos), hexa_str("\\")))))) {
                                        pos = hexa_add(pos, hexa_int(1));
                                        col = hexa_add(col, hexa_int(1));
                                        if (hexa_truthy(hexa_bool((pos).i < (total).i))) {
                                            HexaVal esc = hexa_index_get(chars, pos);
                                            if (hexa_truthy(hexa_eq(esc, hexa_str("n")))) {
                                                ch_val = hexa_str("\n");
                                            } else {
                                                if (hexa_truthy(hexa_eq(esc, hexa_str("t")))) {
                                                    ch_val = hexa_str("\t");
                                                } else {
                                                    if (hexa_truthy(hexa_eq(esc, hexa_str("r")))) {
                                                        ch_val = hexa_str("\r");
                                                    } else {
                                                        if (hexa_truthy(hexa_eq(esc, hexa_str("\\")))) {
                                                            ch_val = hexa_str("\\");
                                                        } else {
                                                            if (hexa_truthy(hexa_eq(esc, hexa_str("'")))) {
                                                                ch_val = hexa_str("'");
                                                            } else {
                                                                if (hexa_truthy(hexa_eq(esc, hexa_str("0")))) {
                                                                    ch_val = hexa_str(" ");
                                                                } else {
                                                                    ch_val = hexa_to_string(esc);
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            pos = hexa_add(pos, hexa_int(1));
                                            col = hexa_add(col, hexa_int(1));
                                        }
                                    } else {
                                        if (hexa_truthy(hexa_bool((pos).i < (total).i))) {
                                            ch_val = hexa_to_string(hexa_index_get(chars, pos));
                                            pos = hexa_add(pos, hexa_int(1));
                                            col = hexa_add(col, hexa_int(1));
                                        }
                                    }
                                    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((pos).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, pos), hexa_str("'")))))) {
                                        pos = hexa_add(pos, hexa_int(1));
                                        col = hexa_add(col, hexa_int(1));
                                    }
                                    tokens = hexa_array_push(tokens, Token(hexa_str("CharLit"), ch_val, line, start_col));
                                } else {
                                    HexaVal start_col = col;
                                    if (hexa_truthy(hexa_eq(ch, hexa_str("+")))) {
                                        tokens = hexa_array_push(tokens, Token(hexa_str("Plus"), hexa_str("+"), line, start_col));
                                        pos = hexa_add(pos, hexa_int(1));
                                        col = hexa_add(col, hexa_int(1));
                                    } else {
                                        if (hexa_truthy(hexa_eq(ch, hexa_str("-")))) {
                                            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str(">")))))) {
                                                tokens = hexa_array_push(tokens, Token(hexa_str("Arrow"), hexa_str("->"), line, start_col));
                                                pos = hexa_add(pos, hexa_int(2));
                                                col = hexa_add(col, hexa_int(2));
                                            } else {
                                                tokens = hexa_array_push(tokens, Token(hexa_str("Minus"), hexa_str("-"), line, start_col));
                                                pos = hexa_add(pos, hexa_int(1));
                                                col = hexa_add(col, hexa_int(1));
                                            }
                                        } else {
                                            if (hexa_truthy(hexa_eq(ch, hexa_str("*")))) {
                                                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("*")))))) {
                                                    tokens = hexa_array_push(tokens, Token(hexa_str("Power"), hexa_str("**"), line, start_col));
                                                    pos = hexa_add(pos, hexa_int(2));
                                                    col = hexa_add(col, hexa_int(2));
                                                } else {
                                                    tokens = hexa_array_push(tokens, Token(hexa_str("Star"), hexa_str("*"), line, start_col));
                                                    pos = hexa_add(pos, hexa_int(1));
                                                    col = hexa_add(col, hexa_int(1));
                                                }
                                            } else {
                                                if (hexa_truthy(hexa_eq(ch, hexa_str("/")))) {
                                                    tokens = hexa_array_push(tokens, Token(hexa_str("Slash"), hexa_str("/"), line, start_col));
                                                    pos = hexa_add(pos, hexa_int(1));
                                                    col = hexa_add(col, hexa_int(1));
                                                } else {
                                                    if (hexa_truthy(hexa_eq(ch, hexa_str("%")))) {
                                                        tokens = hexa_array_push(tokens, Token(hexa_str("Percent"), hexa_str("%"), line, start_col));
                                                        pos = hexa_add(pos, hexa_int(1));
                                                        col = hexa_add(col, hexa_int(1));
                                                    } else {
                                                        if (hexa_truthy(hexa_eq(ch, hexa_str("=")))) {
                                                            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("=")))))) {
                                                                tokens = hexa_array_push(tokens, Token(hexa_str("EqEq"), hexa_str("=="), line, start_col));
                                                                pos = hexa_add(pos, hexa_int(2));
                                                                col = hexa_add(col, hexa_int(2));
                                                            } else {
                                                                tokens = hexa_array_push(tokens, Token(hexa_str("Eq"), hexa_str("="), line, start_col));
                                                                pos = hexa_add(pos, hexa_int(1));
                                                                col = hexa_add(col, hexa_int(1));
                                                            }
                                                        } else {
                                                            if (hexa_truthy(hexa_eq(ch, hexa_str("!")))) {
                                                                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("=")))))) {
                                                                    tokens = hexa_array_push(tokens, Token(hexa_str("NotEq"), hexa_str("!="), line, start_col));
                                                                    pos = hexa_add(pos, hexa_int(2));
                                                                    col = hexa_add(col, hexa_int(2));
                                                                } else {
                                                                    tokens = hexa_array_push(tokens, Token(hexa_str("Not"), hexa_str("!"), line, start_col));
                                                                    pos = hexa_add(pos, hexa_int(1));
                                                                    col = hexa_add(col, hexa_int(1));
                                                                }
                                                            } else {
                                                                if (hexa_truthy(hexa_eq(ch, hexa_str("<")))) {
                                                                    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("=")))))) {
                                                                        tokens = hexa_array_push(tokens, Token(hexa_str("LtEq"), hexa_str("<="), line, start_col));
                                                                        pos = hexa_add(pos, hexa_int(2));
                                                                        col = hexa_add(col, hexa_int(2));
                                                                    } else {
                                                                        tokens = hexa_array_push(tokens, Token(hexa_str("Lt"), hexa_str("<"), line, start_col));
                                                                        pos = hexa_add(pos, hexa_int(1));
                                                                        col = hexa_add(col, hexa_int(1));
                                                                    }
                                                                } else {
                                                                    if (hexa_truthy(hexa_eq(ch, hexa_str(">")))) {
                                                                        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("=")))))) {
                                                                            tokens = hexa_array_push(tokens, Token(hexa_str("GtEq"), hexa_str(">="), line, start_col));
                                                                            pos = hexa_add(pos, hexa_int(2));
                                                                            col = hexa_add(col, hexa_int(2));
                                                                        } else {
                                                                            tokens = hexa_array_push(tokens, Token(hexa_str("Gt"), hexa_str(">"), line, start_col));
                                                                            pos = hexa_add(pos, hexa_int(1));
                                                                            col = hexa_add(col, hexa_int(1));
                                                                        }
                                                                    } else {
                                                                        if (hexa_truthy(hexa_eq(ch, hexa_str("&")))) {
                                                                            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("&")))))) {
                                                                                tokens = hexa_array_push(tokens, Token(hexa_str("And"), hexa_str("&&"), line, start_col));
                                                                                pos = hexa_add(pos, hexa_int(2));
                                                                                col = hexa_add(col, hexa_int(2));
                                                                            } else {
                                                                                tokens = hexa_array_push(tokens, Token(hexa_str("BitAnd"), hexa_str("&"), line, start_col));
                                                                                pos = hexa_add(pos, hexa_int(1));
                                                                                col = hexa_add(col, hexa_int(1));
                                                                            }
                                                                        } else {
                                                                            if (hexa_truthy(hexa_eq(ch, hexa_str("|")))) {
                                                                                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("|")))))) {
                                                                                    tokens = hexa_array_push(tokens, Token(hexa_str("Or"), hexa_str("||"), line, start_col));
                                                                                    pos = hexa_add(pos, hexa_int(2));
                                                                                    col = hexa_add(col, hexa_int(2));
                                                                                } else {
                                                                                    tokens = hexa_array_push(tokens, Token(hexa_str("BitOr"), hexa_str("|"), line, start_col));
                                                                                    pos = hexa_add(pos, hexa_int(1));
                                                                                    col = hexa_add(col, hexa_int(1));
                                                                                }
                                                                            } else {
                                                                                if (hexa_truthy(hexa_eq(ch, hexa_str("^")))) {
                                                                                    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("^")))))) {
                                                                                        tokens = hexa_array_push(tokens, Token(hexa_str("Xor"), hexa_str("^^"), line, start_col));
                                                                                        pos = hexa_add(pos, hexa_int(2));
                                                                                        col = hexa_add(col, hexa_int(2));
                                                                                    } else {
                                                                                        tokens = hexa_array_push(tokens, Token(hexa_str("BitXor"), hexa_str("^"), line, start_col));
                                                                                        pos = hexa_add(pos, hexa_int(1));
                                                                                        col = hexa_add(col, hexa_int(1));
                                                                                    }
                                                                                } else {
                                                                                    if (hexa_truthy(hexa_eq(ch, hexa_str("~")))) {
                                                                                        tokens = hexa_array_push(tokens, Token(hexa_str("BitNot"), hexa_str("~"), line, start_col));
                                                                                        pos = hexa_add(pos, hexa_int(1));
                                                                                        col = hexa_add(col, hexa_int(1));
                                                                                    } else {
                                                                                        if (hexa_truthy(hexa_eq(ch, hexa_str("?")))) {
                                                                                            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("?")))))) {
                                                                                                tokens = hexa_array_push(tokens, Token(hexa_str("NullCoal"), hexa_str("??"), line, start_col));
                                                                                                pos = hexa_add(pos, hexa_int(2));
                                                                                                col = hexa_add(col, hexa_int(2));
                                                                                            } else {
                                                                                                tokens = hexa_array_push(tokens, Token(hexa_str("Question"), hexa_str("?"), line, start_col));
                                                                                                pos = hexa_add(pos, hexa_int(1));
                                                                                                col = hexa_add(col, hexa_int(1));
                                                                                            }
                                                                                        } else {
                                                                                        if (hexa_truthy(hexa_eq(ch, hexa_str(":")))) {
                                                                                            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("=")))))) {
                                                                                                tokens = hexa_array_push(tokens, Token(hexa_str("ColonEq"), hexa_str(":="), line, start_col));
                                                                                                pos = hexa_add(pos, hexa_int(2));
                                                                                                col = hexa_add(col, hexa_int(2));
                                                                                            } else {
                                                                                                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str(":")))))) {
                                                                                                    tokens = hexa_array_push(tokens, Token(hexa_str("ColonColon"), hexa_str("::"), line, start_col));
                                                                                                    pos = hexa_add(pos, hexa_int(2));
                                                                                                    col = hexa_add(col, hexa_int(2));
                                                                                                } else {
                                                                                                    tokens = hexa_array_push(tokens, Token(hexa_str("Colon"), hexa_str(":"), line, start_col));
                                                                                                    pos = hexa_add(pos, hexa_int(1));
                                                                                                    col = hexa_add(col, hexa_int(1));
                                                                                                }
                                                                                            }
                                                                                        } else {
                                                                                            if (hexa_truthy(hexa_eq(ch, hexa_str(".")))) {
                                                                                                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str(".")))))) {
                                                                                                    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(2))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(2))), hexa_str("=")))))) {
                                                                                                        tokens = hexa_array_push(tokens, Token(hexa_str("DotDotEq"), hexa_str("..="), line, start_col));
                                                                                                        pos = hexa_add(pos, hexa_int(3));
                                                                                                        col = hexa_add(col, hexa_int(3));
                                                                                                    } else {
                                                                                                        tokens = hexa_array_push(tokens, Token(hexa_str("DotDot"), hexa_str(".."), line, start_col));
                                                                                                        pos = hexa_add(pos, hexa_int(2));
                                                                                                        col = hexa_add(col, hexa_int(2));
                                                                                                    }
                                                                                                } else {
                                                                                                    tokens = hexa_array_push(tokens, Token(hexa_str("Dot"), hexa_str("."), line, start_col));
                                                                                                    pos = hexa_add(pos, hexa_int(1));
                                                                                                    col = hexa_add(col, hexa_int(1));
                                                                                                }
                                                                                            } else {
                                                                                                if (hexa_truthy(hexa_eq(ch, hexa_str("(")))) {
                                                                                                    tokens = hexa_array_push(tokens, Token(hexa_str("LParen"), hexa_str("("), line, start_col));
                                                                                                    pos = hexa_add(pos, hexa_int(1));
                                                                                                    col = hexa_add(col, hexa_int(1));
                                                                                                } else {
                                                                                                    if (hexa_truthy(hexa_eq(ch, hexa_str(")")))) {
                                                                                                        tokens = hexa_array_push(tokens, Token(hexa_str("RParen"), hexa_str(")"), line, start_col));
                                                                                                        pos = hexa_add(pos, hexa_int(1));
                                                                                                        col = hexa_add(col, hexa_int(1));
                                                                                                    } else {
                                                                                                        if (hexa_truthy(hexa_eq(ch, hexa_str("{")))) {
                                                                                                            tokens = hexa_array_push(tokens, Token(hexa_str("LBrace"), hexa_str("{"), line, start_col));
                                                                                                            pos = hexa_add(pos, hexa_int(1));
                                                                                                            col = hexa_add(col, hexa_int(1));
                                                                                                        } else {
                                                                                                            if (hexa_truthy(hexa_eq(ch, hexa_str("}")))) {
                                                                                                                tokens = hexa_array_push(tokens, Token(hexa_str("RBrace"), hexa_str("}"), line, start_col));
                                                                                                                pos = hexa_add(pos, hexa_int(1));
                                                                                                                col = hexa_add(col, hexa_int(1));
                                                                                                            } else {
                                                                                                                if (hexa_truthy(hexa_eq(ch, hexa_str("[")))) {
                                                                                                                    tokens = hexa_array_push(tokens, Token(hexa_str("LBracket"), hexa_str("["), line, start_col));
                                                                                                                    pos = hexa_add(pos, hexa_int(1));
                                                                                                                    col = hexa_add(col, hexa_int(1));
                                                                                                                } else {
                                                                                                                    if (hexa_truthy(hexa_eq(ch, hexa_str("]")))) {
                                                                                                                        tokens = hexa_array_push(tokens, Token(hexa_str("RBracket"), hexa_str("]"), line, start_col));
                                                                                                                        pos = hexa_add(pos, hexa_int(1));
                                                                                                                        col = hexa_add(col, hexa_int(1));
                                                                                                                    } else {
                                                                                                                        if (hexa_truthy(hexa_eq(ch, hexa_str(",")))) {
                                                                                                                            tokens = hexa_array_push(tokens, Token(hexa_str("Comma"), hexa_str(","), line, start_col));
                                                                                                                            pos = hexa_add(pos, hexa_int(1));
                                                                                                                            col = hexa_add(col, hexa_int(1));
                                                                                                                        } else {
                                                                                                                            if (hexa_truthy(hexa_eq(ch, hexa_str(";")))) {
                                                                                                                                tokens = hexa_array_push(tokens, Token(hexa_str("Semicolon"), hexa_str(";"), line, start_col));
                                                                                                                                pos = hexa_add(pos, hexa_int(1));
                                                                                                                                col = hexa_add(col, hexa_int(1));
                                                                                                                            } else {
                                                                                                                                if (hexa_truthy(hexa_eq(ch, hexa_str("@")))) {
                                                                                                                                    tokens = hexa_array_push(tokens, Token(hexa_str("At"), hexa_str("@"), line, start_col));
                                                                                                                                    pos = hexa_add(pos, hexa_int(1));
                                                                                                                                    col = hexa_add(col, hexa_int(1));
                                                                                                                                } else {
                                                                                                                                    if (hexa_truthy(hexa_eq(ch, hexa_str("#")))) {
                                                                                                                                        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((hexa_add(pos, hexa_int(1))).i < (total).i)) && hexa_truthy(hexa_eq(hexa_index_get(chars, hexa_add(pos, hexa_int(1))), hexa_str("{")))))) {
                                                                                                                                            tokens = hexa_array_push(tokens, Token(hexa_str("HashLBrace"), hexa_str("#{"), line, start_col));
                                                                                                                                            pos = hexa_add(pos, hexa_int(2));
                                                                                                                                            col = hexa_add(col, hexa_int(2));
                                                                                                                                        } else {
                                                                                                                                            pos = hexa_add(pos, hexa_int(1));
                                                                                                                                            col = hexa_add(col, hexa_int(1));
                                                                                                                                        }
                                                                                                                                    } else {
                                                                                                                                        pos = hexa_add(pos, hexa_int(1));
                                                                                                                                        col = hexa_add(col, hexa_int(1));
                                                                                                                                    }
                                                                                                                                }
                                                                                                                            }
                                                                                                                        }
                                                                                                                    }
                                                                                                                }
                                                                                                            }
                                                                                                        }
                                                                                                    }
                                                                                                }
                                                                                            }
                                                                                        }
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    tokens = hexa_array_push(tokens, Token(hexa_str("Eof"), hexa_str(""), line, col));
    return tokens;
    return hexa_void();
}



// === type_checker ===

HexaVal tc_error(HexaVal msg);
HexaVal tc_error_at(HexaVal name, HexaVal msg);
HexaVal tc_push_scope(void);
HexaVal tc_pop_scope(void);
HexaVal tc_define(HexaVal name, HexaVal typ);
HexaVal tc_lookup(HexaVal name);
HexaVal tc_register_fn(HexaVal name, HexaVal arity, HexaVal ret_type);
HexaVal tc_lookup_fn_arity(HexaVal name);
HexaVal tc_lookup_fn_ret(HexaVal name);
HexaVal tc_register_struct(HexaVal name, HexaVal field_count);
HexaVal tc_register_struct_field(HexaVal fname, HexaVal ftype);
HexaVal tc_register_enum(HexaVal name, HexaVal variant_count);
HexaVal tc_register_enum_variant(HexaVal vname);
HexaVal tc_lookup_struct(HexaVal name);
HexaVal tc_lookup_struct_field(HexaVal struct_name, HexaVal field_name);
HexaVal tc_get_struct_field_names(HexaVal struct_name);
HexaVal tc_lookup_enum(HexaVal name);
HexaVal tc_get_enum_variants(HexaVal name);
HexaVal tc_warn(HexaVal msg);
HexaVal tc_warn_at(HexaVal name, HexaVal msg);
HexaVal tc_register_generic_struct(HexaVal name, HexaVal tparams);
HexaVal tc_register_generic_fn(HexaVal name, HexaVal tparams);
HexaVal tc_lookup_generic_struct_param_count(HexaVal name);
HexaVal tc_get_generic_struct_params(HexaVal name);
HexaVal tc_get_generic_struct_bounds(HexaVal name);
HexaVal tc_lookup_generic_fn_param_count(HexaVal name);
HexaVal tc_get_generic_fn_params(HexaVal name);
HexaVal tc_get_generic_fn_bounds(HexaVal name);
HexaVal tc_push_type_params(HexaVal tparams);
HexaVal tc_pop_type_params(HexaVal tparams);
HexaVal tc_is_type_param(HexaVal name);
HexaVal tc_get_type_param_bound(HexaVal name);
HexaVal tc_validate_type_args(HexaVal struct_name, HexaVal type_args);
HexaVal tc_validate_fn_type_args(HexaVal fn_name, HexaVal type_args);
HexaVal tc_register_builtins(void);
HexaVal annotation_to_type(HexaVal ann);
HexaVal types_compatible(HexaVal expected, HexaVal actual);
HexaVal tc_infer_expr(HexaVal node);
HexaVal tc_check_stmt(HexaVal node);
HexaVal tc_check_stmts(HexaVal stmts);
HexaVal tc_first_pass(HexaVal stmts);
HexaVal type_check(HexaVal stmts);
HexaVal type_check_ok(HexaVal stmts);
HexaVal print_type_errors(HexaVal errors);
HexaVal type_errors_count(HexaVal errors);
HexaVal type_errors_has(HexaVal errors, HexaVal substr);
HexaVal type_warnings_count(HexaVal errors);
HexaVal mk(HexaVal kind, HexaVal name, HexaVal value, HexaVal op, HexaVal left, HexaVal right, HexaVal params, HexaVal body, HexaVal args, HexaVal fields, HexaVal variants, HexaVal arms, HexaVal ret_type);
HexaVal nd(HexaVal kind, HexaVal name);
HexaVal lit(HexaVal kind, HexaVal val);
HexaVal let_stmt(HexaVal name, HexaVal init);
HexaVal expr_stmt(HexaVal e);
HexaVal ret_stmt(HexaVal e);
HexaVal fn_decl(HexaVal name, HexaVal params, HexaVal body, HexaVal ret);
HexaVal call_node(HexaVal callee, HexaVal args);
HexaVal field_node(HexaVal obj, HexaVal fname);
HexaVal binop_node(HexaVal op, HexaVal left, HexaVal right);
HexaVal struct_decl_node(HexaVal name, HexaVal fields);
HexaVal struct_field_node(HexaVal name, HexaVal typ);
HexaVal struct_init_node(HexaVal name, HexaVal fields);
HexaVal field_init_node(HexaVal name, HexaVal val);
HexaVal enum_decl_node(HexaVal name, HexaVal variants);
HexaVal enum_variant_node(HexaVal name);
HexaVal match_expr_node(HexaVal scrutinee, HexaVal arms);
HexaVal match_arm_node(HexaVal pattern, HexaVal body);
HexaVal param_node(HexaVal name, HexaVal typ);

HexaVal tc_errors;
HexaVal scope_names;
HexaVal scope_types;
HexaVal scope_depths;
HexaVal tc_depth;
HexaVal fn_names;
HexaVal fn_arities;
HexaVal fn_ret_types;
HexaVal struct_names;
HexaVal struct_field_counts;
HexaVal struct_field_offsets;
HexaVal struct_all_field_names;
HexaVal struct_all_field_types;
HexaVal enum_names;
HexaVal enum_variant_counts;
HexaVal enum_variant_offsets;
HexaVal enum_all_variant_names;
HexaVal current_fn_ret_type;
HexaVal current_fn_name;
HexaVal generic_struct_names;
HexaVal generic_struct_param_counts;
HexaVal generic_struct_param_offsets;
HexaVal generic_all_param_names;
HexaVal generic_all_param_bounds;
HexaVal generic_fn_names;
HexaVal generic_fn_param_counts;
HexaVal generic_fn_param_offsets;
HexaVal generic_fn_all_param_names;
HexaVal generic_fn_all_param_bounds;
HexaVal tparam_names;
HexaVal tparam_bound;
HexaVal test_pass;
HexaVal s1;
HexaVal p2;
HexaVal body2;
HexaVal args2a;
HexaVal s2a;
HexaVal args2b;
HexaVal s2b;
HexaVal sf3;
HexaVal fi3;
HexaVal s3a;
HexaVal s3b;
HexaVal body4;
HexaVal s4;
HexaVal ev5;
HexaVal ab5a;
HexaVal arms5a;
HexaVal s5a;
HexaVal ab5b;
HexaVal arms5b;
HexaVal s5b;
HexaVal s6;
HexaVal fi7;
HexaVal s7;
HexaVal body8;
HexaVal s8;
HexaVal fi9;
HexaVal s9;
HexaVal inner_body;
HexaVal outer_body;
HexaVal s10;



HexaVal AstNode(HexaVal kind, HexaVal name, HexaVal value, HexaVal op, HexaVal left, HexaVal right, HexaVal cond, HexaVal then_body, HexaVal else_body, HexaVal params, HexaVal body, HexaVal args, HexaVal fields, HexaVal items, HexaVal variants, HexaVal arms, HexaVal iter_expr, HexaVal ret_type, HexaVal target, HexaVal trait_name, HexaVal methods) {
    HexaVal __s = hexa_map_new();
    __s = hexa_map_set(__s, "__type__", hexa_str("AstNode"));
    __s = hexa_map_set(__s, "kind", kind);
    __s = hexa_map_set(__s, "name", name);
    __s = hexa_map_set(__s, "value", value);
    __s = hexa_map_set(__s, "op", op);
    __s = hexa_map_set(__s, "left", left);
    __s = hexa_map_set(__s, "right", right);
    __s = hexa_map_set(__s, "cond", cond);
    __s = hexa_map_set(__s, "then_body", then_body);
    __s = hexa_map_set(__s, "else_body", else_body);
    __s = hexa_map_set(__s, "params", params);
    __s = hexa_map_set(__s, "body", body);
    __s = hexa_map_set(__s, "args", args);
    __s = hexa_map_set(__s, "fields", fields);
    __s = hexa_map_set(__s, "items", items);
    __s = hexa_map_set(__s, "variants", variants);
    __s = hexa_map_set(__s, "arms", arms);
    __s = hexa_map_set(__s, "iter_expr", iter_expr);
    __s = hexa_map_set(__s, "ret_type", ret_type);
    __s = hexa_map_set(__s, "target", target);
    __s = hexa_map_set(__s, "trait_name", trait_name);
    __s = hexa_map_set(__s, "methods", methods);
    return __s;
}


HexaVal TypeError(HexaVal message, HexaVal name) {
    HexaVal __s = hexa_map_new();
    __s = hexa_map_set(__s, "__type__", hexa_str("TypeError"));
    __s = hexa_map_set(__s, "message", message);
    __s = hexa_map_set(__s, "name", name);
    return __s;
}


HexaVal tc_error(HexaVal msg) {
    tc_errors = hexa_array_push(tc_errors, TypeError(msg, hexa_str("")));
    return hexa_void();
}


HexaVal tc_error_at(HexaVal name, HexaVal msg) {
    tc_errors = hexa_array_push(tc_errors, TypeError(msg, name));
    return hexa_void();
}


HexaVal TypeErrorEx(HexaVal message, HexaVal name, HexaVal severity) {
    HexaVal __s = hexa_map_new();
    __s = hexa_map_set(__s, "__type__", hexa_str("TypeErrorEx"));
    __s = hexa_map_set(__s, "message", message);
    __s = hexa_map_set(__s, "name", name);
    __s = hexa_map_set(__s, "severity", severity);
    return __s;
}


HexaVal tc_push_scope(void) {
    tc_depth = hexa_add(tc_depth, hexa_int(1));
    return hexa_void();
}


HexaVal tc_pop_scope(void) {
    HexaVal new_names = hexa_array_new();
    HexaVal new_types = hexa_array_new();
    HexaVal new_depths = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(scope_names))).i))) {
        if (hexa_truthy(hexa_bool((hexa_index_get(scope_depths, i)).i < (tc_depth).i))) {
            new_names = hexa_array_push(new_names, hexa_index_get(scope_names, i));
            new_types = hexa_array_push(new_types, hexa_index_get(scope_types, i));
            new_depths = hexa_array_push(new_depths, hexa_index_get(scope_depths, i));
        }
        i = hexa_add(i, hexa_int(1));
    }
    scope_names = new_names;
    scope_types = new_types;
    scope_depths = new_depths;
    tc_depth = hexa_int((tc_depth).i - (hexa_int(1)).i);
    return hexa_void();
}


HexaVal tc_define(HexaVal name, HexaVal typ) {
    scope_names = hexa_array_push(scope_names, name);
    scope_types = hexa_array_push(scope_types, typ);
    scope_depths = hexa_array_push(scope_depths, tc_depth);
    return hexa_void();
}


HexaVal tc_lookup(HexaVal name) {
    HexaVal i = hexa_int((hexa_int(hexa_len(scope_names))).i - (hexa_int(1)).i);
    while (hexa_truthy(hexa_bool((i).i >= (hexa_int(0)).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(scope_names, i), name))) {
            return hexa_index_get(scope_types, i);
        }
        i = hexa_int((i).i - (hexa_int(1)).i);
    }
    return hexa_str("");
    return hexa_void();
}


HexaVal tc_register_fn(HexaVal name, HexaVal arity, HexaVal ret_type) {
    fn_names = hexa_array_push(fn_names, name);
    fn_arities = hexa_array_push(fn_arities, arity);
    fn_ret_types = hexa_array_push(fn_ret_types, ret_type);
    return hexa_void();
}


HexaVal tc_lookup_fn_arity(HexaVal name) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(fn_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(fn_names, i), name))) {
            return hexa_index_get(fn_arities, i);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_int(-(hexa_int(1)).i);
    return hexa_void();
}


HexaVal tc_lookup_fn_ret(HexaVal name) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(fn_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(fn_names, i), name))) {
            return hexa_index_get(fn_ret_types, i);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_str("unknown");
    return hexa_void();
}


HexaVal tc_register_struct(HexaVal name, HexaVal field_count) {
    struct_names = hexa_array_push(struct_names, name);
    struct_field_counts = hexa_array_push(struct_field_counts, field_count);
    struct_field_offsets = hexa_array_push(struct_field_offsets, hexa_int(hexa_len(struct_all_field_names)));
    return hexa_void();
}


HexaVal tc_register_struct_field(HexaVal fname, HexaVal ftype) {
    struct_all_field_names = hexa_array_push(struct_all_field_names, fname);
    struct_all_field_types = hexa_array_push(struct_all_field_types, ftype);
    return hexa_void();
}


HexaVal tc_register_enum(HexaVal name, HexaVal variant_count) {
    enum_names = hexa_array_push(enum_names, name);
    enum_variant_counts = hexa_array_push(enum_variant_counts, variant_count);
    enum_variant_offsets = hexa_array_push(enum_variant_offsets, hexa_int(hexa_len(enum_all_variant_names)));
    return hexa_void();
}


HexaVal tc_register_enum_variant(HexaVal vname) {
    enum_all_variant_names = hexa_array_push(enum_all_variant_names, vname);
    return hexa_void();
}


HexaVal tc_lookup_struct(HexaVal name) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(struct_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(struct_names, i), name))) {
            return hexa_index_get(struct_field_counts, i);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_int(-(hexa_int(1)).i);
    return hexa_void();
}


HexaVal tc_lookup_struct_field(HexaVal struct_name, HexaVal field_name) {
    HexaVal si = hexa_int(0);
    while (hexa_truthy(hexa_bool((si).i < (hexa_int(hexa_len(struct_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(struct_names, si), struct_name))) {
            HexaVal offset = hexa_index_get(struct_field_offsets, si);
            HexaVal count = hexa_index_get(struct_field_counts, si);
            HexaVal fi = hexa_int(0);
            while (hexa_truthy(hexa_bool((fi).i < (count).i))) {
                if (hexa_truthy(hexa_eq(hexa_index_get(struct_all_field_names, hexa_add(offset, fi)), field_name))) {
                    return hexa_index_get(struct_all_field_types, hexa_add(offset, fi));
                }
                fi = hexa_add(fi, hexa_int(1));
            }
            return hexa_str("");
        }
        si = hexa_add(si, hexa_int(1));
    }
    return hexa_str("");
    return hexa_void();
}


HexaVal tc_get_struct_field_names(HexaVal struct_name) {
    HexaVal result = hexa_array_new();
    HexaVal si = hexa_int(0);
    while (hexa_truthy(hexa_bool((si).i < (hexa_int(hexa_len(struct_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(struct_names, si), struct_name))) {
            HexaVal offset = hexa_index_get(struct_field_offsets, si);
            HexaVal count = hexa_index_get(struct_field_counts, si);
            HexaVal fi = hexa_int(0);
            while (hexa_truthy(hexa_bool((fi).i < (count).i))) {
                result = hexa_array_push(result, hexa_index_get(struct_all_field_names, hexa_add(offset, fi)));
                fi = hexa_add(fi, hexa_int(1));
            }
            return result;
        }
        si = hexa_add(si, hexa_int(1));
    }
    return result;
    return hexa_void();
}


HexaVal tc_lookup_enum(HexaVal name) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(enum_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(enum_names, i), name))) {
            return hexa_index_get(enum_variant_counts, i);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_int(-(hexa_int(1)).i);
    return hexa_void();
}


HexaVal tc_get_enum_variants(HexaVal name) {
    HexaVal result = hexa_array_new();
    HexaVal ei = hexa_int(0);
    while (hexa_truthy(hexa_bool((ei).i < (hexa_int(hexa_len(enum_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(enum_names, ei), name))) {
            HexaVal offset = hexa_index_get(enum_variant_offsets, ei);
            HexaVal count = hexa_index_get(enum_variant_counts, ei);
            HexaVal vi = hexa_int(0);
            while (hexa_truthy(hexa_bool((vi).i < (count).i))) {
                result = hexa_array_push(result, hexa_index_get(enum_all_variant_names, hexa_add(offset, vi)));
                vi = hexa_add(vi, hexa_int(1));
            }
            return result;
        }
        ei = hexa_add(ei, hexa_int(1));
    }
    return result;
    return hexa_void();
}


HexaVal tc_warn(HexaVal msg) {
    tc_errors = hexa_array_push(tc_errors, TypeError(hexa_add(hexa_str("[warn] "), msg), hexa_str("")));
    return hexa_void();
}


HexaVal tc_warn_at(HexaVal name, HexaVal msg) {
    tc_errors = hexa_array_push(tc_errors, TypeError(hexa_add(hexa_str("[warn] "), msg), name));
    return hexa_void();
}


HexaVal tc_register_generic_struct(HexaVal name, HexaVal tparams) {
    HexaVal count = hexa_int(hexa_len(tparams));
    if (hexa_truthy(hexa_eq(count, hexa_int(0)))) {
        return hexa_void();
    }
    generic_struct_names = hexa_array_push(generic_struct_names, name);
    generic_struct_param_counts = hexa_array_push(generic_struct_param_counts, count);
    generic_struct_param_offsets = hexa_array_push(generic_struct_param_offsets, hexa_int(hexa_len(generic_all_param_names)));
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (count).i))) {
        generic_all_param_names = hexa_array_push(generic_all_param_names, hexa_map_get(hexa_index_get(tparams, i), "name"));
        HexaVal bound = hexa_map_get(hexa_index_get(tparams, i), "value");
        if (hexa_truthy(hexa_eq(bound, hexa_str("")))) {
            generic_all_param_bounds = hexa_array_push(generic_all_param_bounds, hexa_str(""));
        } else {
            generic_all_param_bounds = hexa_array_push(generic_all_param_bounds, bound);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_void();
}


HexaVal tc_register_generic_fn(HexaVal name, HexaVal tparams) {
    HexaVal count = hexa_int(hexa_len(tparams));
    if (hexa_truthy(hexa_eq(count, hexa_int(0)))) {
        return hexa_void();
    }
    generic_fn_names = hexa_array_push(generic_fn_names, name);
    generic_fn_param_counts = hexa_array_push(generic_fn_param_counts, count);
    generic_fn_param_offsets = hexa_array_push(generic_fn_param_offsets, hexa_int(hexa_len(generic_fn_all_param_names)));
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (count).i))) {
        generic_fn_all_param_names = hexa_array_push(generic_fn_all_param_names, hexa_map_get(hexa_index_get(tparams, i), "name"));
        HexaVal bound = hexa_map_get(hexa_index_get(tparams, i), "value");
        if (hexa_truthy(hexa_eq(bound, hexa_str("")))) {
            generic_fn_all_param_bounds = hexa_array_push(generic_fn_all_param_bounds, hexa_str(""));
        } else {
            generic_fn_all_param_bounds = hexa_array_push(generic_fn_all_param_bounds, bound);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_void();
}


HexaVal tc_lookup_generic_struct_param_count(HexaVal name) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(generic_struct_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(generic_struct_names, i), name))) {
            return hexa_index_get(generic_struct_param_counts, i);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_int(0);
    return hexa_void();
}


HexaVal tc_get_generic_struct_params(HexaVal name) {
    HexaVal result = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(generic_struct_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(generic_struct_names, i), name))) {
            HexaVal offset = hexa_index_get(generic_struct_param_offsets, i);
            HexaVal count = hexa_index_get(generic_struct_param_counts, i);
            HexaVal j = hexa_int(0);
            while (hexa_truthy(hexa_bool((j).i < (count).i))) {
                result = hexa_array_push(result, hexa_index_get(generic_all_param_names, hexa_add(offset, j)));
                j = hexa_add(j, hexa_int(1));
            }
            return result;
        }
        i = hexa_add(i, hexa_int(1));
    }
    return result;
    return hexa_void();
}


HexaVal tc_get_generic_struct_bounds(HexaVal name) {
    HexaVal result = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(generic_struct_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(generic_struct_names, i), name))) {
            HexaVal offset = hexa_index_get(generic_struct_param_offsets, i);
            HexaVal count = hexa_index_get(generic_struct_param_counts, i);
            HexaVal j = hexa_int(0);
            while (hexa_truthy(hexa_bool((j).i < (count).i))) {
                result = hexa_array_push(result, hexa_index_get(generic_all_param_bounds, hexa_add(offset, j)));
                j = hexa_add(j, hexa_int(1));
            }
            return result;
        }
        i = hexa_add(i, hexa_int(1));
    }
    return result;
    return hexa_void();
}


HexaVal tc_lookup_generic_fn_param_count(HexaVal name) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(generic_fn_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(generic_fn_names, i), name))) {
            return hexa_index_get(generic_fn_param_counts, i);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_int(0);
    return hexa_void();
}


HexaVal tc_get_generic_fn_params(HexaVal name) {
    HexaVal result = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(generic_fn_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(generic_fn_names, i), name))) {
            HexaVal offset = hexa_index_get(generic_fn_param_offsets, i);
            HexaVal count = hexa_index_get(generic_fn_param_counts, i);
            HexaVal j = hexa_int(0);
            while (hexa_truthy(hexa_bool((j).i < (count).i))) {
                result = hexa_array_push(result, hexa_index_get(generic_fn_all_param_names, hexa_add(offset, j)));
                j = hexa_add(j, hexa_int(1));
            }
            return result;
        }
        i = hexa_add(i, hexa_int(1));
    }
    return result;
    return hexa_void();
}


HexaVal tc_get_generic_fn_bounds(HexaVal name) {
    HexaVal result = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(generic_fn_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(generic_fn_names, i), name))) {
            HexaVal offset = hexa_index_get(generic_fn_param_offsets, i);
            HexaVal count = hexa_index_get(generic_fn_param_counts, i);
            HexaVal j = hexa_int(0);
            while (hexa_truthy(hexa_bool((j).i < (count).i))) {
                result = hexa_array_push(result, hexa_index_get(generic_fn_all_param_bounds, hexa_add(offset, j)));
                j = hexa_add(j, hexa_int(1));
            }
            return result;
        }
        i = hexa_add(i, hexa_int(1));
    }
    return result;
    return hexa_void();
}


HexaVal tc_push_type_params(HexaVal tparams) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(tparams))).i))) {
        HexaVal tp = hexa_index_get(tparams, i);
        tparam_names = hexa_array_push(tparam_names, hexa_map_get(tp, "name"));
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_map_get(tp, "value"), hexa_str("")))))) {
            tparam_bound = hexa_array_push(tparam_bound, hexa_map_get(tp, "value"));
        } else {
            tparam_bound = hexa_array_push(tparam_bound, hexa_str(""));
        }
        tc_define(hexa_map_get(tp, "name"), hexa_str("typeparam"));
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_void();
}


HexaVal tc_pop_type_params(HexaVal tparams) {
    HexaVal count = hexa_int(hexa_len(tparams));
    if (hexa_truthy(hexa_eq(count, hexa_int(0)))) {
        return hexa_void();
    }
    HexaVal nn = hexa_array_new();
    HexaVal nb = hexa_array_new();
    HexaVal i = hexa_int(0);
    HexaVal target = hexa_int((hexa_int(hexa_len(tparam_names))).i - (count).i);
    while (hexa_truthy(hexa_bool((i).i < (target).i))) {
        nn = hexa_array_push(nn, hexa_index_get(tparam_names, i));
        nb = hexa_array_push(nb, hexa_index_get(tparam_bound, i));
        i = hexa_add(i, hexa_int(1));
    }
    tparam_names = nn;
    tparam_bound = nb;
    return hexa_void();
}


HexaVal tc_is_type_param(HexaVal name) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(tparam_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(tparam_names, i), name))) {
            return hexa_bool(1);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_bool(0);
    return hexa_void();
}


HexaVal tc_get_type_param_bound(HexaVal name) {
    HexaVal i = hexa_int((hexa_int(hexa_len(tparam_names))).i - (hexa_int(1)).i);
    while (hexa_truthy(hexa_bool((i).i >= (hexa_int(0)).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(tparam_names, i), name))) {
            return hexa_index_get(tparam_bound, i);
        }
        i = hexa_int((i).i - (hexa_int(1)).i);
    }
    return hexa_str("");
    return hexa_void();
}


HexaVal tc_validate_type_args(HexaVal struct_name, HexaVal type_args) {
    HexaVal expected = tc_lookup_generic_struct_param_count(struct_name);
    HexaVal actual = hexa_int(hexa_len(type_args));
    if (hexa_truthy(hexa_eq(expected, hexa_int(0)))) {
        if (hexa_truthy(hexa_bool((actual).i > (hexa_int(0)).i))) {
            tc_error(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Struct '"), struct_name), hexa_str("' is not generic but given ")), hexa_to_string(actual)), hexa_str(" type arguments")));
        }
        return hexa_void();
    }
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(actual, expected))))) {
        tc_error(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Struct '"), struct_name), hexa_str("' expects ")), hexa_to_string(expected)), hexa_str(" type params, got ")), hexa_to_string(actual)));
        return hexa_void();
    }
    HexaVal bounds = tc_get_generic_struct_bounds(struct_name);
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (actual).i))) {
        HexaVal bound = hexa_index_get(bounds, i);
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(bound, hexa_str("")))))) {
            HexaVal arg_name = hexa_map_get(hexa_index_get(type_args, i), "name");
            HexaVal arg_type = tc_lookup(arg_name);
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(arg_type, hexa_str(""))) && hexa_truthy(hexa_bool(!hexa_truthy(tc_is_type_param(arg_name))))))) {
                HexaVal at = annotation_to_type(arg_name);
                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(at, arg_name)) && hexa_truthy(hexa_eq(tc_lookup_struct(arg_name), hexa_int(-(hexa_int(1)).i))))) && hexa_truthy(hexa_eq(tc_lookup_enum(arg_name), hexa_int(-(hexa_int(1)).i)))))) {
                    tc_error(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Unknown type '"), arg_name), hexa_str("' used as type argument for '")), struct_name), hexa_str("'")));
                }
            }
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_void();
}


HexaVal tc_validate_fn_type_args(HexaVal fn_name, HexaVal type_args) {
    HexaVal expected = tc_lookup_generic_fn_param_count(fn_name);
    HexaVal actual = hexa_int(hexa_len(type_args));
    if (hexa_truthy(hexa_eq(expected, hexa_int(0)))) {
        if (hexa_truthy(hexa_bool((actual).i > (hexa_int(0)).i))) {
            tc_error(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Function '"), fn_name), hexa_str("' is not generic but given ")), hexa_to_string(actual)), hexa_str(" type arguments")));
        }
        return hexa_void();
    }
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(actual, expected))))) {
        tc_error(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Function '"), fn_name), hexa_str("' expects ")), hexa_to_string(expected)), hexa_str(" type params, got ")), hexa_to_string(actual)));
        return hexa_void();
    }
    return hexa_void();
}


HexaVal tc_register_builtins(void) {
    tc_register_fn(hexa_str("println"), hexa_int(-(hexa_int(1)).i), hexa_str("void"));
    tc_register_fn(hexa_str("print"), hexa_int(-(hexa_int(1)).i), hexa_str("void"));
    tc_register_fn(hexa_str("format"), hexa_int(-(hexa_int(1)).i), hexa_str("string"));
    tc_register_fn(hexa_str("len"), hexa_int(1), hexa_str("int"));
    tc_register_fn(hexa_str("to_string"), hexa_int(1), hexa_str("string"));
    tc_register_fn(hexa_str("is_alpha"), hexa_int(1), hexa_str("bool"));
    tc_register_fn(hexa_str("is_digit"), hexa_int(1), hexa_str("bool"));
    tc_register_fn(hexa_str("is_alphanumeric"), hexa_int(1), hexa_str("bool"));
    tc_register_fn(hexa_str("read_file"), hexa_int(1), hexa_str("string"));
    tc_register_fn(hexa_str("type_of"), hexa_int(1), hexa_str("string"));
    tc_register_fn(hexa_str("int"), hexa_int(1), hexa_str("int"));
    tc_register_fn(hexa_str("float"), hexa_int(1), hexa_str("float"));
    tc_register_fn(hexa_str("string"), hexa_int(1), hexa_str("string"));
    tc_register_fn(hexa_str("push"), hexa_int(2), hexa_str("array"));
    tc_register_fn(hexa_str("sigma"), hexa_int(1), hexa_str("int"));
    tc_register_fn(hexa_str("phi"), hexa_int(1), hexa_str("int"));
    tc_register_fn(hexa_str("tau"), hexa_int(1), hexa_str("int"));
    return hexa_void();
}


HexaVal annotation_to_type(HexaVal ann) {
    if (hexa_truthy(hexa_eq(ann, hexa_str("")))) {
        return hexa_str("unknown");
    }
    if (hexa_truthy(hexa_eq(ann, hexa_str("int")))) {
        return hexa_str("int");
    }
    if (hexa_truthy(hexa_eq(ann, hexa_str("float")))) {
        return hexa_str("float");
    }
    if (hexa_truthy(hexa_eq(ann, hexa_str("bool")))) {
        return hexa_str("bool");
    }
    if (hexa_truthy(hexa_eq(ann, hexa_str("string")))) {
        return hexa_str("string");
    }
    if (hexa_truthy(hexa_eq(ann, hexa_str("char")))) {
        return hexa_str("char");
    }
    if (hexa_truthy(hexa_eq(ann, hexa_str("void")))) {
        return hexa_str("void");
    }
    if (hexa_truthy(hexa_eq(ann, hexa_str("any")))) {
        return hexa_str("any");
    }
    if (hexa_truthy(hexa_eq(ann, hexa_str("array")))) {
        return hexa_str("array");
    }
    if (hexa_truthy(tc_is_type_param(ann))) {
        return ann;
    }
    return ann;
    return hexa_void();
}


HexaVal types_compatible(HexaVal expected, HexaVal actual) {
    if (hexa_truthy(hexa_eq(expected, hexa_str("unknown")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(actual, hexa_str("unknown")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(expected, hexa_str("any")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(actual, hexa_str("any")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(expected, actual))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(expected, hexa_str("float"))) && hexa_truthy(hexa_eq(actual, hexa_str("int")))))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(expected, hexa_str("typeparam")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(actual, hexa_str("typeparam")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(tc_is_type_param(expected))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(tc_is_type_param(actual))) {
        return hexa_bool(1);
    }
    return hexa_bool(0);
    return hexa_void();
}


HexaVal tc_infer_expr(HexaVal node) {
    if (hexa_truthy(hexa_eq(node, hexa_str("")))) {
        return hexa_str("void");
    }
    HexaVal k = hexa_map_get(node, "kind");
    if (hexa_truthy(hexa_eq(k, hexa_str("IntLit")))) {
        return hexa_str("int");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("FloatLit")))) {
        return hexa_str("float");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("BoolLit")))) {
        return hexa_str("bool");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("StringLit")))) {
        return hexa_str("string");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("CharLit")))) {
        return hexa_str("char");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Array")))) {
        return hexa_str("array");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Tuple")))) {
        return hexa_str("tuple");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Wildcard")))) {
        return hexa_str("any");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Range")))) {
        tc_infer_expr(hexa_map_get(node, "left"));
        tc_infer_expr(hexa_map_get(node, "right"));
        return hexa_str("range");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Lambda")))) {
        return hexa_str("fn");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Ident")))) {
        HexaVal typ = tc_lookup(hexa_map_get(node, "name"));
        if (hexa_truthy(hexa_eq(typ, hexa_str("")))) {
            tc_error(hexa_add(hexa_add(hexa_str("Undeclared variable '"), hexa_map_get(node, "name")), hexa_str("'")));
            return hexa_str("unknown");
        }
        return typ;
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("BinOp")))) {
        HexaVal lt = tc_infer_expr(hexa_map_get(node, "left"));
        HexaVal rt = tc_infer_expr(hexa_map_get(node, "right"));
        HexaVal op = hexa_map_get(node, "op");
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(op, hexa_str("=="))) || hexa_truthy(hexa_eq(op, hexa_str("!="))))) || hexa_truthy(hexa_eq(op, hexa_str("<"))))) || hexa_truthy(hexa_eq(op, hexa_str(">"))))) || hexa_truthy(hexa_eq(op, hexa_str("<="))))) || hexa_truthy(hexa_eq(op, hexa_str(">=")))))) {
            return hexa_str("bool");
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("??")))) {
            return rt;
        }
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(op, hexa_str("&&"))) || hexa_truthy(hexa_eq(op, hexa_str("||"))))) || hexa_truthy(hexa_eq(op, hexa_str("^^")))))) {
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(lt, hexa_str("bool"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(lt, hexa_str("unknown")))))))) {
                tc_error(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Logical operator '"), op), hexa_str("' expects bool, got '")), lt), hexa_str("'")));
            }
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(rt, hexa_str("bool"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(rt, hexa_str("unknown")))))))) {
                tc_error(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Logical operator '"), op), hexa_str("' expects bool, got '")), rt), hexa_str("'")));
            }
            return hexa_str("bool");
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("+")))) {
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(lt, hexa_str("string"))) || hexa_truthy(hexa_eq(rt, hexa_str("string")))))) {
                return hexa_str("string");
            }
        }
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(op, hexa_str("+"))) || hexa_truthy(hexa_eq(op, hexa_str("-"))))) || hexa_truthy(hexa_eq(op, hexa_str("*"))))) || hexa_truthy(hexa_eq(op, hexa_str("/"))))) || hexa_truthy(hexa_eq(op, hexa_str("%"))))) || hexa_truthy(hexa_eq(op, hexa_str("**")))))) {
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(lt, hexa_str("float"))) || hexa_truthy(hexa_eq(rt, hexa_str("float")))))) {
                return hexa_str("float");
            }
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(lt, hexa_str("int"))) && hexa_truthy(hexa_eq(rt, hexa_str("int")))))) {
                return hexa_str("int");
            }
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(types_compatible(lt, rt)))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(lt, hexa_str("unknown"))))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(rt, hexa_str("unknown")))))))) {
                tc_error(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Arithmetic type mismatch: '"), lt), hexa_str("' ")), op), hexa_str(" '")), rt), hexa_str("'")));
            }
            return lt;
        }
        return hexa_str("unknown");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("UnaryOp")))) {
        HexaVal ot = tc_infer_expr(hexa_map_get(node, "left"));
        if (hexa_truthy(hexa_eq(hexa_map_get(node, "op"), hexa_str("!")))) {
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(ot, hexa_str("bool"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(ot, hexa_str("unknown")))))))) {
                tc_error(hexa_add(hexa_add(hexa_str("'!' operator expects bool, got '"), ot), hexa_str("'")));
            }
            return hexa_str("bool");
        }
        if (hexa_truthy(hexa_eq(hexa_map_get(node, "op"), hexa_str("-")))) {
            return ot;
        }
        return ot;
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Call")))) {
        HexaVal callee = hexa_map_get(node, "left");
        HexaVal arg_count = hexa_int(hexa_len(hexa_map_get(node, "args")));
        HexaVal i = hexa_int(0);
        while (hexa_truthy(hexa_bool((i).i < (arg_count).i))) {
            tc_infer_expr(hexa_index_get(hexa_map_get(node, "args"), i));
            i = hexa_add(i, hexa_int(1));
        }
        if (hexa_truthy(hexa_eq(hexa_map_get(callee, "kind"), hexa_str("Ident")))) {
            HexaVal fn_arity = tc_lookup_fn_arity(hexa_map_get(callee, "name"));
            if (hexa_truthy(hexa_eq(fn_arity, hexa_int(-(hexa_int(1)).i)))) {
                return hexa_str("unknown");
            }
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool((fn_arity).i >= (hexa_int(0)).i)) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(fn_arity, arg_count))))))) {
                tc_error(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Function '"), hexa_map_get(callee, "name")), hexa_str("' expects ")), hexa_to_string(fn_arity)), hexa_str(" args, got ")), hexa_to_string(arg_count)));
            }
            return tc_lookup_fn_ret(hexa_map_get(callee, "name"));
        }
        if (hexa_truthy(hexa_eq(hexa_map_get(callee, "kind"), hexa_str("Field")))) {
            tc_infer_expr(hexa_map_get(callee, "left"));
            return hexa_str("unknown");
        }
        return hexa_str("unknown");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Field")))) {
        HexaVal obj_type = tc_infer_expr(hexa_map_get(node, "left"));
        HexaVal field_name = hexa_map_get(node, "name");
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(obj_type, hexa_str("unknown"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(obj_type, hexa_str("any"))))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(tc_lookup_struct(obj_type), hexa_int(-(hexa_int(1)).i)))))))) {
            HexaVal ftype = tc_lookup_struct_field(obj_type, field_name);
            if (hexa_truthy(hexa_eq(ftype, hexa_str("")))) {
                HexaVal known = tc_get_struct_field_names(obj_type);
                HexaVal hint = hexa_str("");
                if (hexa_truthy(hexa_bool((hexa_int(hexa_len(known))).i > (hexa_int(0)).i))) {
                    hint = hexa_str(" (known fields: ");
                    HexaVal fi = hexa_int(0);
                    while (hexa_truthy(hexa_bool((fi).i < (hexa_int(hexa_len(known))).i))) {
                        if (hexa_truthy(hexa_bool((fi).i > (hexa_int(0)).i))) {
                            hint = hexa_add(hint, hexa_str(", "));
                        }
                        hint = hexa_add(hint, hexa_index_get(known, fi));
                        fi = hexa_add(fi, hexa_int(1));
                    }
                    hint = hexa_add(hint, hexa_str(")"));
                }
                tc_error(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Struct '"), obj_type), hexa_str("' has no field '")), field_name), hexa_str("'")), hint));
                return hexa_str("unknown");
            }
            return annotation_to_type(ftype);
        }
        return hexa_str("unknown");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Index")))) {
        tc_infer_expr(hexa_map_get(node, "left"));
        HexaVal idx_type = tc_infer_expr(hexa_map_get(node, "right"));
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(idx_type, hexa_str("int"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(idx_type, hexa_str("unknown")))))))) {
            tc_error(hexa_add(hexa_add(hexa_str("Array index must be int, got '"), idx_type), hexa_str("'")));
        }
        return hexa_str("unknown");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("IfExpr")))) {
        HexaVal cond_type = tc_infer_expr(hexa_map_get(node, "cond"));
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(cond_type, hexa_str("bool"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(cond_type, hexa_str("unknown")))))))) {
            tc_error(hexa_add(hexa_add(hexa_str("If condition must be bool, got '"), cond_type), hexa_str("'")));
        }
        tc_push_scope();
        tc_check_stmts(hexa_map_get(node, "then_body"));
        tc_pop_scope();
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_map_get(node, "else_body"), hexa_str(""))))) && hexa_truthy(hexa_bool((hexa_int(hexa_len(hexa_map_get(node, "else_body")))).i > (hexa_int(0)).i))))) {
            tc_push_scope();
            tc_check_stmts(hexa_map_get(node, "else_body"));
            tc_pop_scope();
        }
        return hexa_str("unknown");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("MatchExpr")))) {
        HexaVal match_type = tc_infer_expr(hexa_map_get(node, "left"));
        HexaVal arm_count = hexa_int(hexa_len(hexa_map_get(node, "arms")));
        HexaVal has_wildcard = hexa_bool(0);
        HexaVal covered_variants = hexa_array_new();
        HexaVal i = hexa_int(0);
        while (hexa_truthy(hexa_bool((i).i < (arm_count).i))) {
            HexaVal arm = hexa_index_get(hexa_map_get(node, "arms"), i);
            HexaVal pat = hexa_map_get(arm, "left");
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(pat, hexa_str(""))))) && hexa_truthy(hexa_eq(hexa_map_get(pat, "kind"), hexa_str("Wildcard")))))) {
                has_wildcard = hexa_bool(1);
            }
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(pat, hexa_str(""))))) && hexa_truthy(hexa_eq(hexa_map_get(pat, "kind"), hexa_str("EnumPath")))))) {
                covered_variants = hexa_array_push(covered_variants, hexa_map_get(pat, "name"));
            }
            tc_push_scope();
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(pat, hexa_str(""))))) && hexa_truthy(hexa_eq(hexa_map_get(pat, "kind"), hexa_str("Ident")))))) {
                tc_define(hexa_map_get(pat, "name"), match_type);
            }
            tc_check_stmts(hexa_map_get(arm, "body"));
            tc_pop_scope();
            i = hexa_add(i, hexa_int(1));
        }
        if (hexa_truthy(hexa_bool(!hexa_truthy(has_wildcard)))) {
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(match_type, hexa_str("unknown"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(match_type, hexa_str("any"))))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(tc_lookup_enum(match_type), hexa_int(-(hexa_int(1)).i)))))))) {
                HexaVal all_variants = tc_get_enum_variants(match_type);
                HexaVal missing = hexa_array_new();
                HexaVal vi = hexa_int(0);
                while (hexa_truthy(hexa_bool((vi).i < (hexa_int(hexa_len(all_variants))).i))) {
                    HexaVal found = hexa_bool(0);
                    HexaVal ci = hexa_int(0);
                    while (hexa_truthy(hexa_bool((ci).i < (hexa_int(hexa_len(covered_variants))).i))) {
                        if (hexa_truthy(hexa_eq(hexa_index_get(covered_variants, ci), hexa_index_get(all_variants, vi)))) {
                            found = hexa_bool(1);
                        }
                        ci = hexa_add(ci, hexa_int(1));
                    }
                    if (hexa_truthy(hexa_bool(!hexa_truthy(found)))) {
                        missing = hexa_array_push(missing, hexa_index_get(all_variants, vi));
                    }
                    vi = hexa_add(vi, hexa_int(1));
                }
                if (hexa_truthy(hexa_bool((hexa_int(hexa_len(missing))).i > (hexa_int(0)).i))) {
                    HexaVal miss_str = hexa_str("");
                    HexaVal mi = hexa_int(0);
                    while (hexa_truthy(hexa_bool((mi).i < (hexa_int(hexa_len(missing))).i))) {
                        if (hexa_truthy(hexa_bool((mi).i > (hexa_int(0)).i))) {
                            miss_str = hexa_add(miss_str, hexa_str(", "));
                        }
                        miss_str = hexa_add(miss_str, hexa_index_get(missing, mi));
                        mi = hexa_add(mi, hexa_int(1));
                    }
                    tc_warn(hexa_add(hexa_str("Match may not be exhaustive — missing variants: "), miss_str));
                }
            } else {
                if (hexa_truthy(hexa_bool((arm_count).i > (hexa_int(0)).i))) {
                    tc_warn(hexa_str("Match has no wildcard '_' arm — may not be exhaustive"));
                }
            }
        }
        return hexa_str("unknown");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("StructInit")))) {
        HexaVal sc = tc_lookup_struct(hexa_map_get(node, "name"));
        if (hexa_truthy(hexa_eq(sc, hexa_int(-(hexa_int(1)).i)))) {
            tc_error(hexa_add(hexa_add(hexa_str("Unknown struct '"), hexa_map_get(node, "name")), hexa_str("'")));
        } else {
            HexaVal si_type_args = hexa_map_get(node, "items");
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(si_type_args, hexa_str(""))))) && hexa_truthy(hexa_bool((hexa_int(hexa_len(si_type_args))).i > (hexa_int(0)).i))))) {
                tc_validate_type_args(hexa_map_get(node, "name"), si_type_args);
            } else {
                HexaVal expected_tp = tc_lookup_generic_struct_param_count(hexa_map_get(node, "name"));
                if (hexa_truthy(hexa_bool((expected_tp).i > (hexa_int(0)).i))) {
                    tc_warn(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Generic struct '"), hexa_map_get(node, "name")), hexa_str("' used without type arguments (expected ")), hexa_to_string(expected_tp)), hexa_str(")")));
                }
            }
            HexaVal i = hexa_int(0);
            while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(hexa_map_get(node, "fields")))).i))) {
                HexaVal fld = hexa_index_get(hexa_map_get(node, "fields"), i);
                HexaVal init_type = tc_infer_expr(hexa_map_get(fld, "left"));
                HexaVal expected_type = tc_lookup_struct_field(hexa_map_get(node, "name"), hexa_map_get(fld, "name"));
                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(expected_type, hexa_str(""))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_map_get(fld, "name"), hexa_str("")))))))) {
                    tc_error(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Struct '"), hexa_map_get(node, "name")), hexa_str("' has no field '")), hexa_map_get(fld, "name")), hexa_str("'")));
                } else {
                    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(expected_type, hexa_str(""))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(init_type, hexa_str("unknown")))))))) {
                        HexaVal et = annotation_to_type(expected_type);
                        if (hexa_truthy(hexa_bool(!hexa_truthy(types_compatible(et, init_type))))) {
                            tc_error_at(hexa_map_get(fld, "name"), hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Field type mismatch: expected '"), et), hexa_str("', got '")), init_type), hexa_str("'")));
                        }
                    }
                }
                i = hexa_add(i, hexa_int(1));
            }
            if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_int(hexa_len(hexa_map_get(node, "fields"))), sc))))) {
                tc_warn(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Struct '"), hexa_map_get(node, "name")), hexa_str("' has ")), hexa_to_string(sc)), hexa_str(" fields, but ")), hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "fields"))))), hexa_str(" provided")));
            }
        }
        return hexa_map_get(node, "name");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("EnumPath")))) {
        return hexa_map_get(node, "name");
    }
    return hexa_str("unknown");
    return hexa_void();
}


HexaVal tc_check_stmt(HexaVal node) {
    HexaVal k = hexa_map_get(node, "kind");
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("LetStmt"))) || hexa_truthy(hexa_eq(k, hexa_str("LetMutStmt")))))) {
        HexaVal typ = tc_infer_expr(hexa_map_get(node, "left"));
        HexaVal ann = annotation_to_type(hexa_map_get(node, "value"));
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(ann, hexa_str("unknown"))))) && hexa_truthy(hexa_bool(!hexa_truthy(types_compatible(ann, typ))))))) {
            tc_error_at(hexa_map_get(node, "name"), hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Type mismatch: declared '"), ann), hexa_str("' but assigned '")), typ), hexa_str("'")));
        }
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(ann, hexa_str("unknown")))))) {
            tc_define(hexa_map_get(node, "name"), ann);
        } else {
            tc_define(hexa_map_get(node, "name"), typ);
        }
        return hexa_void();
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("ConstStmt"))) || hexa_truthy(hexa_eq(k, hexa_str("StaticStmt")))))) {
        HexaVal typ = tc_infer_expr(hexa_map_get(node, "left"));
        HexaVal ann = annotation_to_type(hexa_map_get(node, "value"));
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(ann, hexa_str("unknown")))))) {
            tc_define(hexa_map_get(node, "name"), ann);
        } else {
            tc_define(hexa_map_get(node, "name"), typ);
        }
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("FnDecl")))) {
        HexaVal arity = hexa_int(hexa_len(hexa_map_get(node, "params")));
        HexaVal fn_tparams = hexa_map_get(node, "items");
        HexaVal has_tparams = hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(fn_tparams, hexa_str(""))))) && hexa_truthy(hexa_bool((hexa_int(hexa_len(fn_tparams))).i > (hexa_int(0)).i)));
        if (hexa_truthy(has_tparams)) {
            tc_register_generic_fn(hexa_map_get(node, "name"), fn_tparams);
        }
        HexaVal ret = annotation_to_type(hexa_map_get(node, "ret_type"));
        tc_register_fn(hexa_map_get(node, "name"), arity, ret);
        tc_define(hexa_map_get(node, "name"), hexa_str("fn"));
        HexaVal prev_fn_ret = current_fn_ret_type;
        HexaVal prev_fn_name = current_fn_name;
        current_fn_ret_type = ret;
        current_fn_name = hexa_map_get(node, "name");
        tc_push_scope();
        if (hexa_truthy(has_tparams)) {
            tc_push_type_params(fn_tparams);
        }
        HexaVal i = hexa_int(0);
        while (hexa_truthy(hexa_bool((i).i < (arity).i))) {
            HexaVal p = hexa_index_get(hexa_map_get(node, "params"), i);
            HexaVal ptype = annotation_to_type(hexa_map_get(p, "value"));
            tc_define(hexa_map_get(p, "name"), ptype);
            i = hexa_add(i, hexa_int(1));
        }
        tc_check_stmts(hexa_map_get(node, "body"));
        tc_pop_scope();
        if (hexa_truthy(has_tparams)) {
            tc_pop_type_params(fn_tparams);
        }
        current_fn_ret_type = prev_fn_ret;
        current_fn_name = prev_fn_name;
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("StructDecl")))) {
        HexaVal fc = hexa_int(hexa_len(hexa_map_get(node, "fields")));
        tc_register_struct(hexa_map_get(node, "name"), fc);
        HexaVal st_tparams = hexa_map_get(node, "items");
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(st_tparams, hexa_str(""))))) && hexa_truthy(hexa_bool((hexa_int(hexa_len(st_tparams))).i > (hexa_int(0)).i))))) {
            tc_register_generic_struct(hexa_map_get(node, "name"), st_tparams);
        }
        HexaVal fi = hexa_int(0);
        while (hexa_truthy(hexa_bool((fi).i < (fc).i))) {
            HexaVal f = hexa_index_get(hexa_map_get(node, "fields"), fi);
            tc_register_struct_field(hexa_map_get(f, "name"), hexa_map_get(f, "value"));
            fi = hexa_add(fi, hexa_int(1));
        }
        tc_define(hexa_map_get(node, "name"), hexa_str("type"));
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("EnumDecl")))) {
        HexaVal vc = hexa_int(hexa_len(hexa_map_get(node, "variants")));
        tc_register_enum(hexa_map_get(node, "name"), vc);
        HexaVal vi = hexa_int(0);
        while (hexa_truthy(hexa_bool((vi).i < (vc).i))) {
            tc_register_enum_variant(hexa_map_get(hexa_index_get(hexa_map_get(node, "variants"), vi), "name"));
            vi = hexa_add(vi, hexa_int(1));
        }
        tc_define(hexa_map_get(node, "name"), hexa_str("type"));
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("TraitDecl")))) {
        tc_define(hexa_map_get(node, "name"), hexa_str("type"));
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ImplBlock")))) {
        tc_push_scope();
        HexaVal i = hexa_int(0);
        while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(hexa_map_get(node, "methods")))).i))) {
            tc_check_stmt(hexa_index_get(hexa_map_get(node, "methods"), i));
            i = hexa_add(i, hexa_int(1));
        }
        tc_pop_scope();
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ForStmt")))) {
        tc_infer_expr(hexa_map_get(node, "iter_expr"));
        tc_push_scope();
        tc_define(hexa_map_get(node, "name"), hexa_str("unknown"));
        tc_check_stmts(hexa_map_get(node, "body"));
        tc_pop_scope();
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("WhileStmt")))) {
        HexaVal cond_type = tc_infer_expr(hexa_map_get(node, "cond"));
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(cond_type, hexa_str("bool"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(cond_type, hexa_str("unknown")))))))) {
            tc_error(hexa_add(hexa_add(hexa_str("While condition must be bool, got '"), cond_type), hexa_str("'")));
        }
        tc_push_scope();
        tc_check_stmts(hexa_map_get(node, "body"));
        tc_pop_scope();
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("LoopStmt")))) {
        tc_push_scope();
        tc_check_stmts(hexa_map_get(node, "body"));
        tc_pop_scope();
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ReturnStmt")))) {
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_map_get(node, "left"), hexa_str("")))))) {
            HexaVal ret_val_type = tc_infer_expr(hexa_map_get(node, "left"));
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(current_fn_ret_type, hexa_str("unknown"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(current_fn_ret_type, hexa_str("void")))))))) {
                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(types_compatible(current_fn_ret_type, ret_val_type)))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(ret_val_type, hexa_str("unknown")))))))) {
                    tc_error_at(current_fn_name, hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("Return type mismatch: expected '"), current_fn_ret_type), hexa_str("', got '")), ret_val_type), hexa_str("'")));
                }
            }
        } else {
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(current_fn_ret_type, hexa_str("unknown"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(current_fn_ret_type, hexa_str("void")))))))) {
                tc_warn_at(current_fn_name, hexa_add(hexa_add(hexa_str("Bare 'return' in function expecting '"), current_fn_ret_type), hexa_str("'")));
            }
        }
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("AssignStmt")))) {
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_map_get(node, "left"), hexa_str(""))))) && hexa_truthy(hexa_eq(hexa_map_get(hexa_map_get(node, "left"), "kind"), hexa_str("Ident")))))) {
            HexaVal typ = tc_lookup(hexa_map_get(hexa_map_get(node, "left"), "name"));
            if (hexa_truthy(hexa_eq(typ, hexa_str("")))) {
                tc_error(hexa_add(hexa_add(hexa_str("Assignment to undeclared variable '"), hexa_map_get(hexa_map_get(node, "left"), "name")), hexa_str("'")));
            }
        }
        tc_infer_expr(hexa_map_get(node, "left"));
        tc_infer_expr(hexa_map_get(node, "right"));
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ExprStmt")))) {
        tc_infer_expr(hexa_map_get(node, "left"));
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("AssertStmt")))) {
        HexaVal t = tc_infer_expr(hexa_map_get(node, "left"));
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(t, hexa_str("bool"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(t, hexa_str("unknown")))))))) {
            tc_error(hexa_add(hexa_add(hexa_str("Assert expects bool expression, got '"), t), hexa_str("'")));
        }
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("UseStmt")))) {
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ModStmt")))) {
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ImportStmt")))) {
        return hexa_void();
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ProofStmt")))) {
        tc_push_scope();
        tc_check_stmts(hexa_map_get(node, "body"));
        tc_pop_scope();
        return hexa_void();
    }
    return hexa_void();
}


HexaVal tc_check_stmts(HexaVal stmts) {
    if (hexa_truthy(hexa_eq(stmts, hexa_str("")))) {
        return hexa_void();
    }
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(stmts))).i))) {
        tc_check_stmt(hexa_index_get(stmts, i));
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_void();
}


HexaVal tc_first_pass(HexaVal stmts) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(stmts))).i))) {
        HexaVal s = hexa_index_get(stmts, i);
        if (hexa_truthy(hexa_eq(hexa_map_get(s, "kind"), hexa_str("FnDecl")))) {
            tc_register_fn(hexa_map_get(s, "name"), hexa_int(hexa_len(hexa_map_get(s, "params"))), annotation_to_type(hexa_map_get(s, "ret_type")));
            tc_define(hexa_map_get(s, "name"), hexa_str("fn"));
            HexaVal fp_tparams = hexa_map_get(s, "items");
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(fp_tparams, hexa_str(""))))) && hexa_truthy(hexa_bool((hexa_int(hexa_len(fp_tparams))).i > (hexa_int(0)).i))))) {
                tc_register_generic_fn(hexa_map_get(s, "name"), fp_tparams);
            }
        }
        if (hexa_truthy(hexa_eq(hexa_map_get(s, "kind"), hexa_str("StructDecl")))) {
            HexaVal fc = hexa_int(hexa_len(hexa_map_get(s, "fields")));
            tc_register_struct(hexa_map_get(s, "name"), fc);
            HexaVal fi = hexa_int(0);
            while (hexa_truthy(hexa_bool((fi).i < (fc).i))) {
                tc_register_struct_field(hexa_map_get(hexa_index_get(hexa_map_get(s, "fields"), fi), "name"), hexa_map_get(hexa_index_get(hexa_map_get(s, "fields"), fi), "value"));
                fi = hexa_add(fi, hexa_int(1));
            }
            HexaVal sp_tparams = hexa_map_get(s, "items");
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(sp_tparams, hexa_str(""))))) && hexa_truthy(hexa_bool((hexa_int(hexa_len(sp_tparams))).i > (hexa_int(0)).i))))) {
                tc_register_generic_struct(hexa_map_get(s, "name"), sp_tparams);
            }
            tc_define(hexa_map_get(s, "name"), hexa_str("type"));
        }
        if (hexa_truthy(hexa_eq(hexa_map_get(s, "kind"), hexa_str("EnumDecl")))) {
            HexaVal vc = hexa_int(hexa_len(hexa_map_get(s, "variants")));
            tc_register_enum(hexa_map_get(s, "name"), vc);
            HexaVal vi = hexa_int(0);
            while (hexa_truthy(hexa_bool((vi).i < (vc).i))) {
                tc_register_enum_variant(hexa_map_get(hexa_index_get(hexa_map_get(s, "variants"), vi), "name"));
                vi = hexa_add(vi, hexa_int(1));
            }
            tc_define(hexa_map_get(s, "name"), hexa_str("type"));
        }
        if (hexa_truthy(hexa_eq(hexa_map_get(s, "kind"), hexa_str("TraitDecl")))) {
            tc_define(hexa_map_get(s, "name"), hexa_str("type"));
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_void();
}


HexaVal type_check(HexaVal stmts) {
    tc_errors = hexa_array_new();
    scope_names = hexa_array_new();
    scope_types = hexa_array_new();
    scope_depths = hexa_array_new();
    tc_depth = hexa_int(0);
    fn_names = hexa_array_new();
    fn_arities = hexa_array_new();
    fn_ret_types = hexa_array_new();
    struct_names = hexa_array_new();
    struct_field_counts = hexa_array_new();
    struct_field_offsets = hexa_array_new();
    struct_all_field_names = hexa_array_new();
    struct_all_field_types = hexa_array_new();
    enum_names = hexa_array_new();
    enum_variant_counts = hexa_array_new();
    enum_variant_offsets = hexa_array_new();
    enum_all_variant_names = hexa_array_new();
    current_fn_ret_type = hexa_str("unknown");
    current_fn_name = hexa_str("");
    generic_struct_names = hexa_array_new();
    generic_struct_param_counts = hexa_array_new();
    generic_struct_param_offsets = hexa_array_new();
    generic_all_param_names = hexa_array_new();
    generic_all_param_bounds = hexa_array_new();
    generic_fn_names = hexa_array_new();
    generic_fn_param_counts = hexa_array_new();
    generic_fn_param_offsets = hexa_array_new();
    generic_fn_all_param_names = hexa_array_new();
    generic_fn_all_param_bounds = hexa_array_new();
    tparam_names = hexa_array_new();
    tparam_bound = hexa_array_new();
    tc_register_builtins();
    tc_first_pass(stmts);
    tc_check_stmts(stmts);
    return tc_errors;
    return hexa_void();
}


HexaVal type_check_ok(HexaVal stmts) {
    HexaVal errors = type_check(stmts);
    return hexa_eq(hexa_int(hexa_len(errors)), hexa_int(0));
    return hexa_void();
}


HexaVal print_type_errors(HexaVal errors) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(errors))).i))) {
        HexaVal e = hexa_index_get(errors, i);
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_map_get(e, "name"), hexa_str("")))))) {
            hexa_println(hexa_format_n(hexa_str("  TYPE ERROR [{}]: {}"), hexa_array_push(hexa_array_push(hexa_array_new(), hexa_map_get(e, "name")), hexa_map_get(e, "message"))));
        } else {
            hexa_println(hexa_format_n(hexa_str("  TYPE ERROR: {}"), hexa_array_push(hexa_array_new(), hexa_map_get(e, "message"))));
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_void();
}


HexaVal type_errors_count(HexaVal errors) {
    return hexa_int(hexa_len(errors));
    return hexa_void();
}


HexaVal type_errors_has(HexaVal errors, HexaVal substr) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(errors))).i))) {
        HexaVal msg = hexa_map_get(hexa_index_get(errors, i), "message");
        if (hexa_truthy(hexa_bool(hexa_str_contains(msg, substr)))) {
            return hexa_bool(1);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_bool(0);
    return hexa_void();
}


HexaVal type_warnings_count(HexaVal errors) {
    HexaVal count = hexa_int(0);
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(errors))).i))) {
        if (hexa_truthy(hexa_bool(hexa_str_contains(hexa_map_get(hexa_index_get(errors, i), "message"), hexa_str("[warn]"))))) {
            count = hexa_add(count, hexa_int(1));
        }
        i = hexa_add(i, hexa_int(1));
    }
    return count;
    return hexa_void();
}


HexaVal mk(HexaVal kind, HexaVal name, HexaVal value, HexaVal op, HexaVal left, HexaVal right, HexaVal params, HexaVal body, HexaVal args, HexaVal fields, HexaVal variants, HexaVal arms, HexaVal ret_type) {
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "kind", kind), "name", name), "value", value), "op", op), "left", left), "right", right), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", params), "body", body), "args", args), "fields", fields), "items", hexa_str("")), "variants", variants), "arms", arms), "iter_expr", hexa_str("")), "ret_type", ret_type), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal nd(HexaVal kind, HexaVal name) {
    return mk(kind, name, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal lit(HexaVal kind, HexaVal val) {
    return mk(kind, hexa_str(""), val, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal let_stmt(HexaVal name, HexaVal init) {
    return mk(hexa_str("LetStmt"), name, hexa_str(""), hexa_str(""), init, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal expr_stmt(HexaVal e) {
    return mk(hexa_str("ExprStmt"), hexa_str(""), hexa_str(""), hexa_str(""), e, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal ret_stmt(HexaVal e) {
    return mk(hexa_str("ReturnStmt"), hexa_str(""), hexa_str(""), hexa_str(""), e, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal fn_decl(HexaVal name, HexaVal params, HexaVal body, HexaVal ret) {
    return mk(hexa_str("FnDecl"), name, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), params, body, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), ret);
    return hexa_void();
}


HexaVal call_node(HexaVal callee, HexaVal args) {
    return mk(hexa_str("Call"), hexa_str(""), hexa_str(""), hexa_str(""), callee, hexa_str(""), hexa_str(""), hexa_str(""), args, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal field_node(HexaVal obj, HexaVal fname) {
    return mk(hexa_str("Field"), fname, hexa_str(""), hexa_str(""), obj, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal binop_node(HexaVal op, HexaVal left, HexaVal right) {
    return mk(hexa_str("BinOp"), hexa_str(""), hexa_str(""), op, left, right, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal struct_decl_node(HexaVal name, HexaVal fields) {
    return mk(hexa_str("StructDecl"), name, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), fields, hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal struct_field_node(HexaVal name, HexaVal typ) {
    return mk(hexa_str("StructField"), name, typ, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal struct_init_node(HexaVal name, HexaVal fields) {
    return mk(hexa_str("StructInit"), name, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), fields, hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal field_init_node(HexaVal name, HexaVal val) {
    return mk(hexa_str("FieldInit"), name, hexa_str(""), hexa_str(""), val, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal enum_decl_node(HexaVal name, HexaVal variants) {
    return mk(hexa_str("EnumDecl"), name, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), variants, hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal enum_variant_node(HexaVal name) {
    return nd(hexa_str("EnumVariant"), name);
    return hexa_void();
}


HexaVal match_expr_node(HexaVal scrutinee, HexaVal arms) {
    return mk(hexa_str("MatchExpr"), hexa_str(""), hexa_str(""), hexa_str(""), scrutinee, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), arms, hexa_str(""));
    return hexa_void();
}


HexaVal match_arm_node(HexaVal pattern, HexaVal body) {
    return mk(hexa_str("MatchArm"), hexa_str(""), hexa_str(""), hexa_str(""), pattern, hexa_str(""), hexa_str(""), body, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}


HexaVal param_node(HexaVal name, HexaVal typ) {
    return mk(hexa_str("Param"), name, typ, hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""), hexa_str(""));
    return hexa_void();
}



// === codegen_c2 ===

HexaVal codegen_c2(HexaVal ast);
HexaVal gen2_fn_forward(HexaVal node);
HexaVal gen2_fn_decl(HexaVal node);
// bt 72: forward decls so gen2_fn_decl can call the hoist helpers
HexaVal _gen2_collect_lets(HexaVal stmts, HexaVal names);
HexaVal _gen2_has_decl(HexaVal name);
HexaVal gen2_extern_ret_kind(HexaVal ret_type);
HexaVal gen2_ffi_c_type(HexaVal typ);
HexaVal gen2_ffi_c_ret_type(HexaVal ret_type);
HexaVal gen2_has_float_param(HexaVal params);
HexaVal gen2_ffi_marshal_arg(HexaVal param_name, HexaVal param_type);
HexaVal gen2_ffi_wrap_ret(HexaVal ret_type, HexaVal var_name);
HexaVal gen2_static_c_type(HexaVal typ);
HexaVal gen2_extern_static_decl(HexaVal node);
HexaVal gen2_extern_includes(HexaVal ast);
HexaVal gen2_static_ffi_block(HexaVal ast);
HexaVal gen2_extern_wrapper(HexaVal node);
HexaVal gen2_indent(HexaVal n);
HexaVal gen2_stmt(HexaVal node, HexaVal depth);
HexaVal c_escape(HexaVal s);
HexaVal gen2_method_builtin(HexaVal obj_expr, HexaVal method, HexaVal args);
HexaVal gen2_expr(HexaVal node);
HexaVal gen2_struct_decl(HexaVal node);
HexaVal gen2_enum_decl(HexaVal node);
HexaVal gen2_match_stmt(HexaVal node, HexaVal depth);
HexaVal gen2_match_cond(HexaVal pat, HexaVal scrutinee_var);
HexaVal gen2_match_expr(HexaVal node);
HexaVal gen2_match_ternary(HexaVal arms, HexaVal scrutinee_c, HexaVal idx);
HexaVal gen2_arm_value(HexaVal body);
HexaVal _method_registry_lookup(HexaVal name);
HexaVal gen2_impl_block(HexaVal node);
HexaVal _is_known_fn_global(HexaVal name);
HexaVal _is_known_nonlocal(HexaVal name);
HexaVal _is_known_global(HexaVal name);
HexaVal _is_builtin_name(HexaVal name);
HexaVal _add_unique(HexaVal list, HexaVal name);
HexaVal gen2_collect_free(HexaVal node, HexaVal bound, HexaVal out);
HexaVal gen2_lambda_expr(HexaVal node);
HexaVal codegen_c2_full(HexaVal ast);

HexaVal _method_registry;
HexaVal _lambda_counter;
HexaVal _lambda_def_parts;
HexaVal _known_fn_globals;
HexaVal _known_nonlocal_names;
// bt 72: per-function set of C identifiers already declared. Hexa permits
// `let x = ...` shadow-rebind in the same block; C forbids it. We track
// declared names here and emit subsequent `let x = ...` as plain assignment.
HexaVal _gen2_declared_names;

/* rt#37: inline-cache slot accumulator (static defs for hexa_map_get_ic). */
HexaVal _ic_def_parts;
int _ic_def_parts_init = 0;

HexaVal codegen_c2(HexaVal ast) {
    HexaVal parts = hexa_array_new();
    parts = hexa_array_push(parts, hexa_str("// Generated by HEXA self-host compiler\n"));
    parts = hexa_array_push(parts, hexa_str("#include \"runtime.c\"\n#include <unistd.h>\n\n"));
    _known_fn_globals = hexa_array_new();
    _known_nonlocal_names = hexa_array_new();
    _lambda_counter = hexa_int(0);
    _lambda_def_parts = hexa_array_new();
    _gen2_declared_names = hexa_array_new();
    /* rt#37: reset inline-cache slot accumulator per codegen invocation. */
    _ic_def_parts = hexa_array_new();
    _ic_def_parts_init = 1;
    HexaVal _gi = hexa_int(0);
    while (hexa_truthy(hexa_bool((_gi).i < (hexa_int(hexa_len(ast))).i))) {
        HexaVal _gk = hexa_map_get(hexa_index_get(ast, _gi), "kind");
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(_gk, hexa_str("FnDecl"))) || hexa_truthy(hexa_eq(_gk, hexa_str("ExternFnDecl"))))) || hexa_truthy(hexa_eq(_gk, hexa_str("PureFnDecl"))))) || hexa_truthy(hexa_eq(_gk, hexa_str("OptimizeFnStmt")))))) {
            _known_fn_globals = hexa_array_push(_known_fn_globals, hexa_map_get(hexa_index_get(ast, _gi), "name"));
            _known_nonlocal_names = hexa_array_push(_known_nonlocal_names, hexa_map_get(hexa_index_get(ast, _gi), "name"));
        } else {
            if (hexa_truthy(hexa_eq(_gk, hexa_str("StructDecl")))) {
                _known_fn_globals = hexa_array_push(_known_fn_globals, hexa_map_get(hexa_index_get(ast, _gi), "name"));
                _known_nonlocal_names = hexa_array_push(_known_nonlocal_names, hexa_map_get(hexa_index_get(ast, _gi), "name"));
            } else {
                if (hexa_truthy(hexa_eq(_gk, hexa_str("EnumDecl")))) {
                    _known_nonlocal_names = hexa_array_push(_known_nonlocal_names, hexa_map_get(hexa_index_get(ast, _gi), "name"));
                } else {
                    if (hexa_truthy(hexa_eq(_gk, hexa_str("ComptimeConst")))) {
                        _known_nonlocal_names = hexa_array_push(_known_nonlocal_names, hexa_map_get(hexa_index_get(ast, _gi), "name"));
                    } else {
                        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(_gk, hexa_str("LetMutStmt"))) || hexa_truthy(hexa_eq(_gk, hexa_str("LetStmt")))))) {
                            _known_nonlocal_names = hexa_array_push(_known_nonlocal_names, hexa_map_get(hexa_index_get(ast, _gi), "name"));
                        }
                    }
                }
            }
        }
        _gi = hexa_add(_gi, hexa_int(1));
    }
    HexaVal fwd_parts = hexa_array_new();
    HexaVal global_parts = hexa_array_new();
    HexaVal fn_parts = hexa_array_new();
    HexaVal main_parts = hexa_array_new();
    HexaVal extern_fns = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(ast))).i))) {
        HexaVal k = hexa_map_get(hexa_index_get(ast, i), "kind");
        if (hexa_truthy(hexa_eq(k, hexa_str("FnDecl")))) {
            fwd_parts = hexa_array_push(fwd_parts, hexa_add(gen2_fn_forward(hexa_index_get(ast, i)), hexa_str("\n")));
            fn_parts = hexa_array_push(fn_parts, hexa_add(gen2_fn_decl(hexa_index_get(ast, i)), hexa_str("\n\n")));
        } else {
            if (hexa_truthy(hexa_eq(k, hexa_str("ExternFnDecl")))) {
                extern_fns = hexa_array_push(extern_fns, hexa_map_get(hexa_index_get(ast, i), "name"));
                HexaVal ew = gen2_extern_wrapper(hexa_index_get(ast, i));
                global_parts = hexa_array_push(global_parts, hexa_map_get(ew, "global"));
                fwd_parts = hexa_array_push(fwd_parts, hexa_add(hexa_map_get(ew, "forward"), hexa_str("\n")));
                fn_parts = hexa_array_push(fn_parts, hexa_add(hexa_map_get(ew, "fn_code"), hexa_str("\n\n")));
                main_parts = hexa_array_push(main_parts, hexa_map_get(ew, "init"));
            } else {
                if (hexa_truthy(hexa_eq(k, hexa_str("StructDecl")))) {
                    fn_parts = hexa_array_push(fn_parts, hexa_add(gen2_struct_decl(hexa_index_get(ast, i)), hexa_str("\n\n")));
                } else {
                    if (hexa_truthy(hexa_eq(k, hexa_str("EnumDecl")))) {
                        fn_parts = hexa_array_push(fn_parts, hexa_add(gen2_enum_decl(hexa_index_get(ast, i)), hexa_str("\n")));
                    } else {
                        if (hexa_truthy(hexa_eq(k, hexa_str("ImplBlock")))) {
                            HexaVal ib = gen2_impl_block(hexa_index_get(ast, i));
                            fwd_parts = hexa_array_push(fwd_parts, hexa_map_get(ib, "forward"));
                            fn_parts = hexa_array_push(fn_parts, hexa_map_get(ib, "fn_code"));
                        } else {
                            if (hexa_truthy(hexa_eq(k, hexa_str("TraitDecl")))) {
                            } else {
                                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("LetMutStmt"))) || hexa_truthy(hexa_eq(k, hexa_str("LetStmt")))))) {
                                    /* bt-53: mangle reserved runtime names (TAG_INT etc.) */
                                    HexaVal __user_name = hexa_mangle_ident(hexa_map_get(hexa_index_get(ast, i), "name"));
                                    global_parts = hexa_array_push(global_parts, hexa_add(hexa_add(hexa_str("HexaVal "), __user_name), hexa_str(";\n")));
                                    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(hexa_index_get(ast, i), "left")), hexa_str("string")))))) {
                                        main_parts = hexa_array_push(main_parts, hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("    "), __user_name), hexa_str(" = ")), gen2_expr(hexa_map_get(hexa_index_get(ast, i), "left"))), hexa_str(";\n")));
                                    } else {
                                        main_parts = hexa_array_push(main_parts, hexa_add(hexa_add(hexa_str("    "), __user_name), hexa_str(" = hexa_void();\n")));
                                    }
                                } else {
                                    main_parts = hexa_array_push(main_parts, gen2_stmt(hexa_index_get(ast, i), hexa_int(1)));
                                }
                            }
                        }
                    }
                }
            }
        }
        i = hexa_add(i, hexa_int(1));
    }
    parts = hexa_array_push(parts, hexa_str_join(fwd_parts, hexa_str("")));
    parts = hexa_array_push(parts, hexa_str("\n"));
    parts = hexa_array_push(parts, hexa_str_join(global_parts, hexa_str("")));
    parts = hexa_array_push(parts, hexa_str("\n"));
    /* rt#37: emit inline-cache slot defs before fn bodies reference them. */
    if (_ic_def_parts_init) {
        parts = hexa_array_push(parts, hexa_str_join(_ic_def_parts, hexa_str("")));
        parts = hexa_array_push(parts, hexa_str("\n"));
    }
    parts = hexa_array_push(parts, hexa_str_join(_lambda_def_parts, hexa_str("")));
    parts = hexa_array_push(parts, hexa_str_join(fn_parts, hexa_str("")));
    parts = hexa_array_push(parts, hexa_str("int main(int argc, char** argv) {\n    hexa_set_args(argc, argv);\n"));
    parts = hexa_array_push(parts, hexa_str_join(main_parts, hexa_str("")));
    parts = hexa_array_push(parts, hexa_str("    return 0;\n}\n"));
    return hexa_str_join(parts, hexa_str(""));
    return hexa_void();
}


HexaVal gen2_fn_forward(HexaVal node) {
    HexaVal params = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(hexa_map_get(node, "params")))).i))) {
        params = hexa_array_push(params, hexa_add(hexa_str("HexaVal "), hexa_map_get(hexa_index_get(hexa_map_get(node, "params"), i), "name")));
        i = hexa_add(i, hexa_int(1));
    }
    HexaVal p = hexa_str_join(params, hexa_str(", "));
    if (hexa_truthy(hexa_eq(p, hexa_str("")))) {
        p = hexa_str("void");
    }
    return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("HexaVal "), hexa_mangle_ident(hexa_map_get(node, "name"))), hexa_str("(")), p), hexa_str(");"));
    return hexa_void();
}


HexaVal gen2_fn_decl(HexaVal node) {
    HexaVal params = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(hexa_map_get(node, "params")))).i))) {
        params = hexa_array_push(params, hexa_add(hexa_str("HexaVal "), hexa_map_get(hexa_index_get(hexa_map_get(node, "params"), i), "name")));
        i = hexa_add(i, hexa_int(1));
    }
    HexaVal p = hexa_str_join(params, hexa_str(", "));
    if (hexa_truthy(hexa_eq(p, hexa_str("")))) {
        p = hexa_str("void");
    }
    // bt 72: per-function shadow-handling via hoisting.
    // Pre-scan body to count LetStmt occurrences. Names appearing >1 times
    // (shadowed across branches/loops) are hoisted to the top of the C fn
    // as `HexaVal x = hexa_void();` and LetStmt emits as plain assignment.
    _gen2_declared_names = hexa_array_new();
    HexaVal _pi = hexa_int(0);
    while (hexa_truthy(hexa_bool((_pi).i < (hexa_int(hexa_len(hexa_map_get(node, "params")))).i))) {
        _gen2_declared_names = hexa_array_push(_gen2_declared_names, hexa_map_get(hexa_index_get(hexa_map_get(node, "params"), _pi), "name"));
        _pi = hexa_add(_pi, hexa_int(1));
    }
    // Collect all LetStmt names (with duplicates from multiple branches).
    HexaVal _all_names = _gen2_collect_lets(hexa_map_get(node, "body"), hexa_array_new());
    // Count duplicates: names appearing >1 times need hoisting.
    HexaVal _hoisted = hexa_array_new();
    HexaVal _hoists = hexa_array_new();
    HexaVal _ii = hexa_int(0);
    HexaVal _nn = hexa_int(hexa_len(_all_names));
    while (hexa_truthy(hexa_bool((_ii).i < (_nn).i))) {
        HexaVal _nm = hexa_index_get(_all_names, _ii);
        // Skip if already hoisted or if the name is a parameter (already in
        // _gen2_declared_names via the param seeding above).
        HexaVal _already = _gen2_has_decl(_nm);
        HexaVal _jj = hexa_int(0);
        while (hexa_truthy(hexa_bool((_jj).i < (hexa_int(hexa_len(_hoisted))).i))) {
            if (hexa_truthy(hexa_eq(hexa_index_get(_hoisted, _jj), _nm))) {
                _already = hexa_bool(1);
            }
            _jj = hexa_add(_jj, hexa_int(1));
        }
        if (hexa_truthy(hexa_bool(!hexa_truthy(_already)))) {
            // Count remaining occurrences.
            HexaVal _cnt = hexa_int(0);
            HexaVal _kk = hexa_int(0);
            while (hexa_truthy(hexa_bool((_kk).i < (_nn).i))) {
                if (hexa_truthy(hexa_eq(hexa_index_get(_all_names, _kk), _nm))) {
                    _cnt = hexa_add(_cnt, hexa_int(1));
                }
                _kk = hexa_add(_kk, hexa_int(1));
            }
            if (hexa_truthy(hexa_bool((_cnt).i > (hexa_int(1)).i))) {
                _hoisted = hexa_array_push(_hoisted, _nm);
                _gen2_declared_names = hexa_array_push(_gen2_declared_names, _nm);
                _hoists = hexa_array_push(_hoists, hexa_add(hexa_add(hexa_str("    HexaVal "), _nm), hexa_str(" = hexa_void();\n")));
            }
        }
        _ii = hexa_add(_ii, hexa_int(1));
    }
    HexaVal chunks = hexa_array_new();
    chunks = hexa_array_push(chunks, hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("HexaVal "), hexa_mangle_ident(hexa_map_get(node, "name"))), hexa_str("(")), p), hexa_str(") {\n")));
    HexaVal _hj = hexa_int(0);
    while (hexa_truthy(hexa_bool((_hj).i < (hexa_int(hexa_len(_hoists))).i))) {
        chunks = hexa_array_push(chunks, hexa_index_get(_hoists, _hj));
        _hj = hexa_add(_hj, hexa_int(1));
    }
    HexaVal bi = hexa_int(0);
    while (hexa_truthy(hexa_bool((bi).i < (hexa_int(hexa_len(hexa_map_get(node, "body")))).i))) {
        chunks = hexa_array_push(chunks, gen2_stmt(hexa_index_get(hexa_map_get(node, "body"), bi), hexa_int(1)));
        bi = hexa_add(bi, hexa_int(1));
    }
    chunks = hexa_array_push(chunks, hexa_str("    return hexa_void();\n}\n"));
    return hexa_str_join(chunks, hexa_str(""));
    return hexa_void();
}


HexaVal gen2_extern_ret_kind(HexaVal ret_type) {
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ret_type, hexa_str("Void"))) || hexa_truthy(hexa_eq(ret_type, hexa_str("void"))))) || hexa_truthy(hexa_eq(ret_type, hexa_str("")))))) {
        return hexa_str("0");
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ret_type, hexa_str("Int"))) || hexa_truthy(hexa_eq(ret_type, hexa_str("int")))))) {
        return hexa_str("1");
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ret_type, hexa_str("Float"))) || hexa_truthy(hexa_eq(ret_type, hexa_str("float")))))) {
        return hexa_str("2");
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ret_type, hexa_str("Bool"))) || hexa_truthy(hexa_eq(ret_type, hexa_str("bool")))))) {
        return hexa_str("3");
    }
    return hexa_str("4");
    return hexa_void();
}


HexaVal gen2_ffi_c_type(HexaVal typ) {
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(typ, hexa_str("Float"))) || hexa_truthy(hexa_eq(typ, hexa_str("float")))))) {
        return hexa_str("double");
    }
    return hexa_str("int64_t");
    return hexa_void();
}


HexaVal gen2_ffi_c_ret_type(HexaVal ret_type) {
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ret_type, hexa_str("Void"))) || hexa_truthy(hexa_eq(ret_type, hexa_str("void"))))) || hexa_truthy(hexa_eq(ret_type, hexa_str("")))))) {
        return hexa_str("void");
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ret_type, hexa_str("Float"))) || hexa_truthy(hexa_eq(ret_type, hexa_str("float")))))) {
        return hexa_str("double");
    }
    return hexa_str("int64_t");
    return hexa_void();
}


HexaVal gen2_has_float_param(HexaVal params) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(params))).i))) {
        HexaVal t = hexa_map_get(hexa_index_get(params, i), "value");
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(t, hexa_str("Float"))) || hexa_truthy(hexa_eq(t, hexa_str("float")))))) {
            return hexa_int(1);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_int(0);
    return hexa_void();
}


HexaVal gen2_ffi_marshal_arg(HexaVal param_name, HexaVal param_type) {
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(param_type, hexa_str("Float"))) || hexa_truthy(hexa_eq(param_type, hexa_str("float")))))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("("), param_name), hexa_str(".tag==TAG_FLOAT?")), param_name), hexa_str(".f:(double)")), param_name), hexa_str(".i)"));
    }
    return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("("), param_name), hexa_str(".tag==TAG_INT?")), param_name), hexa_str(".i:(int64_t)")), param_name), hexa_str(".f)"));
    return hexa_void();
}


HexaVal gen2_ffi_wrap_ret(HexaVal ret_type, HexaVal var_name) {
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ret_type, hexa_str("Void"))) || hexa_truthy(hexa_eq(ret_type, hexa_str("void"))))) || hexa_truthy(hexa_eq(ret_type, hexa_str("")))))) {
        return hexa_str("hexa_void()");
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ret_type, hexa_str("Float"))) || hexa_truthy(hexa_eq(ret_type, hexa_str("float")))))) {
        return hexa_add(hexa_add(hexa_str("hexa_float("), var_name), hexa_str(")"));
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ret_type, hexa_str("Bool"))) || hexa_truthy(hexa_eq(ret_type, hexa_str("bool")))))) {
        return hexa_add(hexa_add(hexa_str("hexa_bool("), var_name), hexa_str(" != 0)"));
    }
    return hexa_add(hexa_add(hexa_str("hexa_int("), var_name), hexa_str(")"));
    return hexa_void();
}


HexaVal gen2_static_c_type(HexaVal typ) {
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(typ, hexa_str("i64"))) || hexa_truthy(hexa_eq(typ, hexa_str("Int"))))) || hexa_truthy(hexa_eq(typ, hexa_str("int")))))) {
        return hexa_str("long long");
    }
    if (hexa_truthy(hexa_eq(typ, hexa_str("i32")))) {
        return hexa_str("int");
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(typ, hexa_str("f64"))) || hexa_truthy(hexa_eq(typ, hexa_str("Float"))))) || hexa_truthy(hexa_eq(typ, hexa_str("float")))))) {
        return hexa_str("double");
    }
    if (hexa_truthy(hexa_eq(typ, hexa_str("f32")))) {
        return hexa_str("float");
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(typ, hexa_str("str"))) || hexa_truthy(hexa_eq(typ, hexa_str("Str"))))) || hexa_truthy(hexa_eq(typ, hexa_str("String")))))) {
        return hexa_str("char*");
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(typ, hexa_str("bool"))) || hexa_truthy(hexa_eq(typ, hexa_str("Bool")))))) {
        return hexa_str("int");
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(typ, hexa_str("void"))) || hexa_truthy(hexa_eq(typ, hexa_str("Void"))))) || hexa_truthy(hexa_eq(typ, hexa_str("")))))) {
        return hexa_str("void");
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(typ, hexa_str("*Void"))) || hexa_truthy(hexa_eq(typ, hexa_str("Ptr")))))) {
        return hexa_str("void*");
    }
    return typ;
    return hexa_void();
}


HexaVal gen2_extern_static_decl(HexaVal node) {
    HexaVal name = hexa_map_get(node, "name");
    HexaVal ret_type = hexa_map_get(node, "ret_type");
    HexaVal c_name = hexa_map_get(node, "value");
    if (hexa_truthy(hexa_eq(c_name, hexa_str("")))) {
        c_name = name;
    }
    HexaVal c_ret = gen2_static_c_type(ret_type);
    HexaVal c_params = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(hexa_map_get(node, "params")))).i))) {
        HexaVal p = hexa_index_get(hexa_map_get(node, "params"), i);
        HexaVal c_typ = gen2_static_c_type(hexa_map_get(p, "value"));
        if (hexa_truthy(hexa_eq(c_typ, hexa_str("void")))) {
            c_params = hexa_array_push(c_params, hexa_add(hexa_str("long long "), hexa_map_get(p, "name")));
        } else {
            c_params = hexa_array_push(c_params, hexa_add(hexa_add(c_typ, hexa_str(" ")), hexa_map_get(p, "name")));
        }
        i = hexa_add(i, hexa_int(1));
    }
    HexaVal params_str = hexa_str_join(c_params, hexa_str(", "));
    HexaVal sig = params_str;
    if (hexa_truthy(hexa_eq(sig, hexa_str("")))) {
        sig = hexa_str("void");
    }
    HexaVal parts = hexa_array_new();
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_map_get(node, "op"), hexa_str("")))))) {
        parts = hexa_array_push(parts, hexa_add(hexa_add(hexa_str("// link: -l"), hexa_map_get(node, "op")), hexa_str("\n")));
    }
    parts = hexa_array_push(parts, hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("extern "), c_ret), hexa_str(" ")), c_name), hexa_str("(")), sig), hexa_str(");\n")));
    return hexa_str_join(parts, hexa_str(""));
    return hexa_void();
}


HexaVal gen2_extern_includes(HexaVal ast) {
    HexaVal needs_stdio = hexa_int(0);
    HexaVal needs_stdlib = hexa_int(0);
    HexaVal needs_string = hexa_int(0);
    HexaVal needs_math = hexa_int(0);
    HexaVal has_extern = hexa_int(0);
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(ast))).i))) {
        if (hexa_truthy(hexa_eq(hexa_map_get(hexa_index_get(ast, i), "kind"), hexa_str("ExternFnDecl")))) {
            has_extern = hexa_int(1);
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(hexa_map_get(hexa_index_get(ast, i), "op"), hexa_str("m"))) || hexa_truthy(hexa_eq(hexa_map_get(hexa_index_get(ast, i), "op"), hexa_str("libm")))))) {
                needs_math = hexa_int(1);
            }
            HexaVal j = hexa_int(0);
            while (hexa_truthy(hexa_bool((j).i < (hexa_int(hexa_len(hexa_map_get(hexa_index_get(ast, i), "params")))).i))) {
                HexaVal t = hexa_map_get(hexa_index_get(hexa_map_get(hexa_index_get(ast, i), "params"), j), "value");
                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(t, hexa_str("str"))) || hexa_truthy(hexa_eq(t, hexa_str("Str"))))) || hexa_truthy(hexa_eq(t, hexa_str("String")))))) {
                    needs_string = hexa_int(1);
                }
                j = hexa_add(j, hexa_int(1));
            }
            HexaVal rt = hexa_map_get(hexa_index_get(ast, i), "ret_type");
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(rt, hexa_str("str"))) || hexa_truthy(hexa_eq(rt, hexa_str("Str"))))) || hexa_truthy(hexa_eq(rt, hexa_str("String")))))) {
                needs_string = hexa_int(1);
            }
        }
        i = hexa_add(i, hexa_int(1));
    }
    if (hexa_truthy(hexa_eq(has_extern, hexa_int(0)))) {
        return hexa_str("");
    }
    HexaVal includes = hexa_array_new();
    includes = hexa_array_push(includes, hexa_str("#include <stdint.h>\n"));
    if (hexa_truthy(hexa_eq(needs_math, hexa_int(1)))) {
        includes = hexa_array_push(includes, hexa_str("#include <math.h>\n"));
    }
    if (hexa_truthy(hexa_eq(needs_string, hexa_int(1)))) {
        includes = hexa_array_push(includes, hexa_str("#include <string.h>\n"));
    }
    return hexa_str_join(includes, hexa_str(""));
    return hexa_void();
}


HexaVal gen2_static_ffi_block(HexaVal ast) {
    HexaVal includes = gen2_extern_includes(ast);
    HexaVal decls = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(ast))).i))) {
        if (hexa_truthy(hexa_eq(hexa_map_get(hexa_index_get(ast, i), "kind"), hexa_str("ExternFnDecl")))) {
            decls = hexa_array_push(decls, gen2_extern_static_decl(hexa_index_get(ast, i)));
        }
        i = hexa_add(i, hexa_int(1));
    }
    if (hexa_truthy(hexa_eq(hexa_int(hexa_len(decls)), hexa_int(0)))) {
        return hexa_str("");
    }
    HexaVal parts = hexa_array_new();
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(includes, hexa_str("")))))) {
        parts = hexa_array_push(parts, includes);
    }
    parts = hexa_array_push(parts, hexa_str("\n// ── FFI declarations ──\n"));
    parts = hexa_array_push(parts, hexa_str_join(decls, hexa_str("")));
    parts = hexa_array_push(parts, hexa_str("\n"));
    return hexa_str_join(parts, hexa_str(""));
    return hexa_void();
}


HexaVal gen2_extern_wrapper(HexaVal node) {
    HexaVal name = hexa_map_get(node, "name");
    HexaVal nargs = hexa_int(hexa_len(hexa_map_get(node, "params")));
    HexaVal ret_type = hexa_map_get(node, "ret_type");
    HexaVal c_sym = hexa_map_get(node, "value");
    if (hexa_truthy(hexa_eq(c_sym, hexa_str("")))) {
        c_sym = name;
    }
    HexaVal global = hexa_add(hexa_add(hexa_str("static void* __ffi_sym_"), name), hexa_str(" = NULL;\n"));
    HexaVal fwd_params = hexa_array_new();
    HexaVal fi = hexa_int(0);
    while (hexa_truthy(hexa_bool((fi).i < (nargs).i))) {
        fwd_params = hexa_array_push(fwd_params, hexa_add(hexa_str("HexaVal "), hexa_map_get(hexa_index_get(hexa_map_get(node, "params"), fi), "name")));
        fi = hexa_add(fi, hexa_int(1));
    }
    HexaVal fwd_p = hexa_str_join(fwd_params, hexa_str(", "));
    HexaVal sig_p = fwd_p;
    if (hexa_truthy(hexa_eq(sig_p, hexa_str("")))) {
        sig_p = hexa_str("void");
    }
    HexaVal forward = hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("HexaVal "), name), hexa_str("(")), sig_p), hexa_str(");"));
    HexaVal has_float = gen2_has_float_param(hexa_map_get(node, "params"));
    HexaVal impl_parts = hexa_array_new();
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(has_float, hexa_int(1))) || hexa_truthy(hexa_bool((nargs).i > (hexa_int(0)).i))))) {
        HexaVal c_ret = gen2_ffi_c_ret_type(ret_type);
        HexaVal typedef_params = hexa_array_new();
        HexaVal ti = hexa_int(0);
        while (hexa_truthy(hexa_bool((ti).i < (nargs).i))) {
            typedef_params = hexa_array_push(typedef_params, gen2_ffi_c_type(hexa_map_get(hexa_index_get(hexa_map_get(node, "params"), ti), "value")));
            ti = hexa_add(ti, hexa_int(1));
        }
        HexaVal td_p = hexa_str_join(typedef_params, hexa_str(", "));
        HexaVal td_sig = td_p;
        if (hexa_truthy(hexa_eq(td_sig, hexa_str("")))) {
            td_sig = hexa_str("void");
        }
        impl_parts = hexa_array_push(impl_parts, hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("typedef "), c_ret), hexa_str(" (*__ffi_ftyp_")), name), hexa_str(")(")), td_sig), hexa_str(");\n")));
        impl_parts = hexa_array_push(impl_parts, hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("HexaVal "), name), hexa_str("(")), sig_p), hexa_str(") {\n")));
        HexaVal call_args = hexa_array_new();
        HexaVal ci = hexa_int(0);
        while (hexa_truthy(hexa_bool((ci).i < (nargs).i))) {
            call_args = hexa_array_push(call_args, gen2_ffi_marshal_arg(hexa_map_get(hexa_index_get(hexa_map_get(node, "params"), ci), "name"), hexa_map_get(hexa_index_get(hexa_map_get(node, "params"), ci), "value")));
            ci = hexa_add(ci, hexa_int(1));
        }
        HexaVal call_str = hexa_str_join(call_args, hexa_str(", "));
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(ret_type, hexa_str("Void"))) || hexa_truthy(hexa_eq(ret_type, hexa_str("void"))))) || hexa_truthy(hexa_eq(ret_type, hexa_str("")))))) {
            impl_parts = hexa_array_push(impl_parts, hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("    ((__ffi_ftyp_"), name), hexa_str(")__ffi_sym_")), name), hexa_str(")(")), call_str), hexa_str(");\n")));
            impl_parts = hexa_array_push(impl_parts, hexa_str("    return hexa_void();\n"));
        } else {
            impl_parts = hexa_array_push(impl_parts, hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("    "), c_ret), hexa_str(" __r = ((__ffi_ftyp_")), name), hexa_str(")__ffi_sym_")), name), hexa_str(")(")), call_str), hexa_str(");\n")));
            impl_parts = hexa_array_push(impl_parts, hexa_add(hexa_add(hexa_str("    return "), gen2_ffi_wrap_ret(ret_type, hexa_str("__r"))), hexa_str(";\n")));
        }
        impl_parts = hexa_array_push(impl_parts, hexa_str("}\n"));
    } else {
        impl_parts = hexa_array_push(impl_parts, hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("HexaVal "), name), hexa_str("(")), sig_p), hexa_str(") {\n")));
        impl_parts = hexa_array_push(impl_parts, hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("    return hexa_extern_call(__ffi_sym_"), name), hexa_str(", NULL, 0, ")), gen2_extern_ret_kind(ret_type)), hexa_str(");\n")));
        impl_parts = hexa_array_push(impl_parts, hexa_str("}\n"));
    }
    HexaVal impl_code = hexa_str_join(impl_parts, hexa_str(""));
    HexaVal lib_arg = hexa_str("NULL");
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_map_get(node, "op"), hexa_str("")))))) {
        lib_arg = hexa_add(hexa_add(hexa_str("\""), hexa_map_get(node, "op")), hexa_str("\""));
    }
    HexaVal init = hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("    __ffi_sym_"), name), hexa_str(" = hexa_ffi_dlsym(hexa_ffi_dlopen(")), lib_arg), hexa_str("), \"")), c_sym), hexa_str("\");\n"));
    return hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_set(hexa_map_new(), "global", global), "forward", forward), "fn_code", impl_code), "init", init), "kind", hexa_str("")), "name", hexa_str("")), "value", hexa_str("")), "op", hexa_str("")), "left", hexa_str("")), "right", hexa_str("")), "cond", hexa_str("")), "then_body", hexa_str("")), "else_body", hexa_str("")), "params", hexa_str("")), "body", hexa_str("")), "args", hexa_str("")), "fields", hexa_str("")), "items", hexa_str("")), "variants", hexa_str("")), "arms", hexa_str("")), "iter_expr", hexa_str("")), "ret_type", hexa_str("")), "target", hexa_str("")), "trait_name", hexa_str("")), "methods", hexa_str(""));
    return hexa_void();
}


HexaVal gen2_indent(HexaVal n) {
    HexaVal parts = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (n).i))) {
        parts = hexa_array_push(parts, hexa_str("    "));
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_str_join(parts, hexa_str(""));
    return hexa_void();
}

// bt 72: return truthy if name hoisted (declared at fn top) for this function.
HexaVal _gen2_has_decl(HexaVal name) {
    HexaVal n = hexa_int(hexa_len(_gen2_declared_names));
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (n).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(_gen2_declared_names, i), name))) {
            return hexa_bool(1);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_bool(0);
}

// bt 72: recursively collect LetStmt names into a flat array (with duplicates)
// from a statement list, including nested if/while/for/match bodies.
HexaVal _gen2_collect_lets(HexaVal stmts, HexaVal names);
HexaVal _gen2_collect_lets_stmt(HexaVal node, HexaVal names) {
    if (hexa_truthy(hexa_eq(hexa_type_of(node), hexa_str("string")))) {
        return names;
    }
    HexaVal sk = hexa_map_get(node, "kind");
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(sk, hexa_str("LetStmt"))) || hexa_truthy(hexa_eq(sk, hexa_str("LetMutStmt")))))) {
        names = hexa_array_push(names, hexa_map_get(node, "name"));
    }
    HexaVal tb = hexa_map_get(node, "then_body");
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(tb), hexa_str("string")))))) {
        names = _gen2_collect_lets(tb, names);
    }
    HexaVal eb = hexa_map_get(node, "else_body");
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(eb), hexa_str("string")))))) {
        names = _gen2_collect_lets(eb, names);
    }
    HexaVal bd = hexa_map_get(node, "body");
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(bd), hexa_str("string")))))) {
        names = _gen2_collect_lets(bd, names);
    }
    HexaVal arms = hexa_map_get(node, "arms");
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(arms), hexa_str("string")))))) {
        HexaVal ai = hexa_int(0);
        while (hexa_truthy(hexa_bool((ai).i < (hexa_int(hexa_len(arms))).i))) {
            HexaVal arm = hexa_index_get(arms, ai);
            if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(arm), hexa_str("string")))))) {
                HexaVal ab = hexa_map_get(arm, "body");
                if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(ab), hexa_str("string")))))) {
                    names = _gen2_collect_lets(ab, names);
                }
            }
            ai = hexa_add(ai, hexa_int(1));
        }
    }
    // bt 72: for ExprStmt-wrapped IfExpr/MatchExpr used as a statement, the
    // if body lives on node.left — recurse only when kind is ExprStmt to
    // avoid walking general expression trees (which can loop/explode).
    if (hexa_truthy(hexa_eq(sk, hexa_str("ExprStmt")))) {
        HexaVal lf = hexa_map_get(node, "left");
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(lf), hexa_str("string")))))) {
            names = _gen2_collect_lets_stmt(lf, names);
        }
    }
    return names;
}
HexaVal _gen2_collect_lets(HexaVal stmts, HexaVal names) {
    if (hexa_truthy(hexa_eq(hexa_type_of(stmts), hexa_str("string")))) {
        return names;
    }
    HexaVal i = hexa_int(0);
    HexaVal n = hexa_int(hexa_len(stmts));
    while (hexa_truthy(hexa_bool((i).i < (n).i))) {
        names = _gen2_collect_lets_stmt(hexa_index_get(stmts, i), names);
        i = hexa_add(i, hexa_int(1));
    }
    return names;
}


// rt#32-N: conservative hot-globals list for snapshot-promote. These are
// the arrays that exhibit the pass-by-value ABA blocker documented in
// rt#32-M when the push-arena path fires. Widening this list is cheap;
// starting narrow minimises blast radius of the transpiler change.
//
// The list covers the 14 interpreter globals from rt#32-M's mitigation
// attempt. A `let x = <name>` where <name> is in this list gets wrapped
// with hexa_val_snapshot_array(...) so the snapshot is insulated from
// subsequent callee arena-rewinds.
static int _gen2_is_snapshot_hot_global(HexaVal name_v) {
    if (name_v.tag != TAG_STR || !name_v.s) return 0;
    const char* n = name_v.s;
    static const char* hot[] = {
        "env_var_names", "env_var_vals", "env_scopes", "env_scope_dirty",
        "env_cache_names", "env_cache_idxs", "env_vars", "env_cache",
        "env_fns", "fn_cache", "call_stack", "defer_stack",
        "gen_buffer", "call_arg_buf",
        NULL
    };
    for (int i = 0; hot[i]; i++) {
        if (strcmp(n, hot[i]) == 0) return 1;
    }
    return 0;
}

// rt#32-N: gate via HEXA_CODEGEN_SNAPSHOT env var. Default ON. Set to "0"
// to regenerate C without snapshot wrappers for a bit-identical comparison.
static int _gen2_snapshot_enabled_cache = -1;
static int _gen2_snapshot_enabled(void) {
    if (_gen2_snapshot_enabled_cache < 0) {
        const char* e = getenv("HEXA_CODEGEN_SNAPSHOT");
        if (!e || !e[0]) _gen2_snapshot_enabled_cache = 1;  // default ON
        else             _gen2_snapshot_enabled_cache = (e[0] != '0') ? 1 : 0;
    }
    return _gen2_snapshot_enabled_cache;
}

// Wrap an init C expression with hexa_val_snapshot_array(...) iff the RHS
// AST node is an Ident referencing a hot global. Low risk: passthrough for
// all other expressions (literals, calls, binops, etc.).
static HexaVal _gen2_maybe_snapshot(HexaVal init_c, HexaVal rhs_node) {
    if (!_gen2_snapshot_enabled()) return init_c;
    if (hexa_truthy(hexa_eq(hexa_type_of(rhs_node), hexa_str("string")))) return init_c;
    HexaVal kind = hexa_map_get(rhs_node, "kind");
    if (!hexa_truthy(hexa_eq(kind, hexa_str("Ident")))) return init_c;
    HexaVal name = hexa_map_get(rhs_node, "name");
    if (!_gen2_is_snapshot_hot_global(name)) return init_c;
    return hexa_add(hexa_add(hexa_str("hexa_val_snapshot_array("), init_c), hexa_str(")"));
}

// rt#32-O (extension): hot-*locals* whose RHS is a slice_fast / method-call on
// a hot global. The interpreter's NK_CALL path does:
//   let call_args = call_arg_buf.slice_fast(_ca_base, call_arg_top)
//   call_arg_buf = call_arg_buf.truncate(_ca_base)
//   callee(call_args)  // recurses and rewinds arena → dangles call_args.items
// The call_args.items pointer lives inside the shared call_arg_buf arena;
// recursive NK_CALLs push+rewind that arena, invalidating the parent slice.
// We cut the alias by promoting `call_args` (and peer locals that follow the
// same capture-then-pass-to-recursive-callee pattern) to heap at capture.
static int _gen2_is_snapshot_hot_local(HexaVal name_v) {
    if (name_v.tag != TAG_STR || !name_v.s) return 0;
    const char* n = name_v.s;
    static const char* hot[] = {
        "call_args",      // interpreter NK_CALL arg slice
        "spread_items",   // G8 spread — `f(...arr)` arena-backed
        "args",           // call_fn_val / call_user_fn parameter lists
        "raw_args",       // AST args list alias before eval loop
        // call_user_fn locals: these hold arena-backed slices of fn_info
        // whose storage lives in the parent eval frame's call_arg_buf
        // window; a recursive callee truncates call_arg_buf and invalidates
        // params/body → infinite fib recursion observed without snapshot.
        "fn_info",
        "params",
        "body",
        // AST nodes that the outer eval_expr/exec_stmt loop reads repeatedly
        // across a nested call. In fib's `f(n-1) + f(n-2)` the Binary handler
        // holds `node`, calls eval_expr(node.left), then eval_expr(node.right).
        // If the nested call's arena rewind clobbers whatever backs `node`,
        // the second dereference returns garbage and we loop.
        "node",
        "expr",
        "stmt",
        "callee",
        NULL
    };
    for (int i = 0; hot[i]; i++) {
        if (strcmp(n, hot[i]) == 0) return 1;
    }
    return 0;
}
static HexaVal _gen2_maybe_snapshot_local(HexaVal init_c, HexaVal lhs_name) {
    if (!_gen2_snapshot_enabled()) return init_c;
    if (!_gen2_is_snapshot_hot_local(lhs_name)) return init_c;
    return hexa_add(hexa_add(hexa_str("hexa_val_snapshot_array("), init_c), hexa_str(")"));
}

// rt#32-O: snapshot-promote for NK_CALL arguments. Root cause: when an arg
// expression yields an arena-backed array (cap<0), a *prior* call in the same
// args-tuple may push+rewind an arena mark, invalidating the stored .items
// pointer. This crash was seen with HEXA_ARRAY_PUSH_ARENA=1 on fib(2):
//   f(n-1) + f(n-2)
// Here n-1's callee pops a mark; the outer call's cached pointer for an arg
// that came back as arena-backed (or referenced one pre-call) aliases freed
// arena memory.
//
// Fix: wrap arg evaluations with hexa_val_snapshot_array(...). Runtime behavior
// is O(1) passthrough for non-arrays (tag!=TAG_ARRAY) and for heap arrays
// (cap>=0); only genuinely arena-backed arrays pay a promote-to-heap memcpy.
//
// Two modes, gated by HEXA_CODEGEN_SNAPSHOT_ARGS:
//   "0"             → off (pre-rt#32-O behavior)
//   "conservative"  → wrap only Ident args that reference hot globals
//   anything else   → aggressive: wrap every non-literal arg  (DEFAULT)
static int _gen2_snapshot_args_mode_cache = -2;  // -2=unread, -1=off, 0=conservative, 1=aggressive
static int _gen2_snapshot_args_mode(void) {
    if (_gen2_snapshot_args_mode_cache == -2) {
        const char* e = getenv("HEXA_CODEGEN_SNAPSHOT_ARGS");
        if (!e || !e[0]) _gen2_snapshot_args_mode_cache = 1;  // default aggressive
        else if (e[0] == '0') _gen2_snapshot_args_mode_cache = -1;
        else if (strcmp(e, "conservative") == 0) _gen2_snapshot_args_mode_cache = 0;
        else _gen2_snapshot_args_mode_cache = 1;
    }
    return _gen2_snapshot_args_mode_cache;
}

// Returns 1 if arg_node is a "trivially non-array" literal that can never
// need snapshot-promote. Covers the overwhelmingly common numeric/string arg
// shapes (fib(n-1) → the "n-1" Binary still wraps under aggressive, but plain
// "42" / "\"s\"" / true / 'c' / 1.5 skip the wrapper).
static int _gen2_arg_is_trivial_literal(HexaVal arg_node) {
    if (hexa_truthy(hexa_eq(hexa_type_of(arg_node), hexa_str("string")))) return 1;
    HexaVal kind = hexa_map_get(arg_node, "kind");
    if (hexa_truthy(hexa_eq(kind, hexa_str("IntLit")))) return 1;
    if (hexa_truthy(hexa_eq(kind, hexa_str("FloatLit")))) return 1;
    if (hexa_truthy(hexa_eq(kind, hexa_str("BoolLit")))) return 1;
    if (hexa_truthy(hexa_eq(kind, hexa_str("CharLit")))) return 1;
    if (hexa_truthy(hexa_eq(kind, hexa_str("StringLit")))) return 1;
    return 0;
}

// Wrap a single arg C expression with hexa_val_snapshot_array(...) per the
// current HEXA_CODEGEN_SNAPSHOT_ARGS mode. Caller must also respect the outer
// HEXA_CODEGEN_SNAPSHOT master gate.
static HexaVal _gen2_maybe_snapshot_arg(HexaVal arg_c, HexaVal arg_node) {
    if (!_gen2_snapshot_enabled()) return arg_c;
    int mode = _gen2_snapshot_args_mode();
    if (mode < 0) return arg_c;  // disabled
    // Trivial literals never carry array items.
    if (_gen2_arg_is_trivial_literal(arg_node)) return arg_c;
    if (mode == 0) {
        // conservative — Ident of hot-global only.
        if (hexa_truthy(hexa_eq(hexa_type_of(arg_node), hexa_str("string")))) return arg_c;
        HexaVal kind = hexa_map_get(arg_node, "kind");
        if (!hexa_truthy(hexa_eq(kind, hexa_str("Ident")))) return arg_c;
        HexaVal name = hexa_map_get(arg_node, "name");
        if (!_gen2_is_snapshot_hot_global(name)) return arg_c;
    }
    // mode == 1 (aggressive): wrap every non-literal.
    return hexa_add(hexa_add(hexa_str("hexa_val_snapshot_array("), arg_c), hexa_str(")"));
}

HexaVal gen2_stmt(HexaVal node, HexaVal depth) {
    HexaVal pad = gen2_indent(depth);
    HexaVal k = hexa_map_get(node, "kind");
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("LetStmt"))) || hexa_truthy(hexa_eq(k, hexa_str("LetMutStmt")))))) {
        HexaVal init = hexa_str("hexa_void()");
        HexaVal rhs_node = hexa_map_get(node, "left");
        HexaVal __lhs_name = hexa_map_get(node, "name");
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(rhs_node), hexa_str("string")))))) {
            init = gen2_expr(rhs_node);
            // rt#32-N: snapshot-wrap hot-global Ident RHS
            init = _gen2_maybe_snapshot(init, rhs_node);
            // rt#32-O (local): snapshot-wrap hot-local LHS RHS (any shape)
            init = _gen2_maybe_snapshot_local(init, __lhs_name);
        }
        HexaVal _ln = hexa_map_get(node, "name");
        // bt 72: if this name was hoisted to function top, emit plain assignment
        // (avoids C "redefinition" errors from Hexa's let-shadow semantics).
        if (hexa_truthy(_gen2_has_decl(_ln))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(pad, _ln), hexa_str(" = ")), init), hexa_str(";\n"));
        }
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("HexaVal ")), _ln), hexa_str(" = ")), init), hexa_str(";\n"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ConstStmt")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("HexaVal ")), hexa_map_get(node, "name")), hexa_str(" = ")), gen2_expr(hexa_map_get(node, "left"))), hexa_str(";\n"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("AssignStmt")))) {
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "left")), hexa_str("string"))))) && hexa_truthy(hexa_eq(hexa_map_get(hexa_map_get(node, "left"), "kind"), hexa_str("Index")))))) {
            HexaVal container = gen2_expr(hexa_map_get(hexa_map_get(node, "left"), "left"));
            HexaVal idx = gen2_expr(hexa_map_get(hexa_map_get(node, "left"), "right"));
            HexaVal val = gen2_expr(hexa_map_get(node, "right"));
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, container), hexa_str(" = hexa_index_set(")), container), hexa_str(", ")), idx), hexa_str(", ")), val), hexa_str(");\n"));
        }
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "left")), hexa_str("string"))))) && hexa_truthy(hexa_eq(hexa_map_get(hexa_map_get(node, "left"), "kind"), hexa_str("Field")))))) {
            HexaVal obj = gen2_expr(hexa_map_get(hexa_map_get(node, "left"), "left"));
            HexaVal field = hexa_map_get(hexa_map_get(node, "left"), "name");
            HexaVal val = gen2_expr(hexa_map_get(node, "right"));
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, obj), hexa_str(" = hexa_map_set(")), obj), hexa_str(", \"")), field), hexa_str("\", ")), val), hexa_str(");\n"));
        }
        return hexa_add(hexa_add(hexa_add(hexa_add(pad, gen2_expr(hexa_map_get(node, "left"))), hexa_str(" = ")), gen2_expr(hexa_map_get(node, "right"))), hexa_str(";\n"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("CompoundAssign")))) {
        HexaVal lhs = gen2_expr(hexa_map_get(node, "left"));
        HexaVal rhs = gen2_expr(hexa_map_get(node, "right"));
        HexaVal op = hexa_map_get(node, "op");
        if (hexa_truthy(hexa_eq(op, hexa_str("+=")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, lhs), hexa_str(" = hexa_add(")), lhs), hexa_str(", ")), rhs), hexa_str(");\n"));
        }
        /* T23 fix: route -= /= *= %= through runtime helpers so that
         * float operands use .f instead of reading .i from a union that
         * was populated via .f (which produces the bit-pattern of the
         * double cast to int64_t → garbage arithmetic). */
        if (hexa_truthy(hexa_eq(op, hexa_str("-=")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, lhs), hexa_str(" = hexa_sub(")), lhs), hexa_str(", ")), rhs), hexa_str(");\n"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("*=")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, lhs), hexa_str(" = hexa_mul(")), lhs), hexa_str(", ")), rhs), hexa_str(");\n"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("/=")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, lhs), hexa_str(" = hexa_div(")), lhs), hexa_str(", ")), rhs), hexa_str(");\n"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("%=")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, lhs), hexa_str(" = hexa_mod(")), lhs), hexa_str(", ")), rhs), hexa_str(");\n"));
        }
        return hexa_add(hexa_add(hexa_add(pad, hexa_str("/* compound ")), op), hexa_str(" */\n"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ReturnStmt")))) {
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "left")), hexa_str("string")))))) {
            return hexa_add(hexa_add(hexa_add(pad, hexa_str("return ")), gen2_expr(hexa_map_get(node, "left"))), hexa_str(";\n"));
        }
        return hexa_add(pad, hexa_str("return hexa_void();\n"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ExprStmt")))) {
        HexaVal expr = hexa_map_get(node, "left");
        if (hexa_truthy(hexa_eq(hexa_type_of(expr), hexa_str("string")))) {
            return hexa_str("");
        }
        if (hexa_truthy(hexa_eq(hexa_map_get(expr, "kind"), hexa_str("MatchExpr")))) {
            return gen2_match_stmt(expr, depth);
        }
        if (hexa_truthy(hexa_eq(hexa_map_get(expr, "kind"), hexa_str("IfExpr")))) {
            HexaVal chunks = hexa_array_new();
            chunks = hexa_array_push(chunks, hexa_add(hexa_add(hexa_add(pad, hexa_str("if (hexa_truthy(")), gen2_expr(hexa_map_get(expr, "cond"))), hexa_str(")) {\n")));
            HexaVal ti = hexa_int(0);
            while (hexa_truthy(hexa_bool((ti).i < (hexa_int(hexa_len(hexa_map_get(expr, "then_body")))).i))) {
                chunks = hexa_array_push(chunks, gen2_stmt(hexa_index_get(hexa_map_get(expr, "then_body"), ti), hexa_add(depth, hexa_int(1))));
                ti = hexa_add(ti, hexa_int(1));
            }
            chunks = hexa_array_push(chunks, hexa_add(pad, hexa_str("}")));
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(expr, "else_body")), hexa_str("string"))))) && hexa_truthy(hexa_bool((hexa_int(hexa_len(hexa_map_get(expr, "else_body")))).i > (hexa_int(0)).i))))) {
                chunks = hexa_array_push(chunks, hexa_str(" else {\n"));
                HexaVal ei = hexa_int(0);
                while (hexa_truthy(hexa_bool((ei).i < (hexa_int(hexa_len(hexa_map_get(expr, "else_body")))).i))) {
                    chunks = hexa_array_push(chunks, gen2_stmt(hexa_index_get(hexa_map_get(expr, "else_body"), ei), hexa_add(depth, hexa_int(1))));
                    ei = hexa_add(ei, hexa_int(1));
                }
                chunks = hexa_array_push(chunks, hexa_add(pad, hexa_str("}")));
            }
            chunks = hexa_array_push(chunks, hexa_str("\n"));
            return hexa_str_join(chunks, hexa_str(""));
        }
        return hexa_add(hexa_add(pad, gen2_expr(expr)), hexa_str(";\n"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("WhileStmt")))) {
        HexaVal chunks = hexa_array_new();
        chunks = hexa_array_push(chunks, hexa_add(hexa_add(hexa_add(pad, hexa_str("while (hexa_truthy(")), gen2_expr(hexa_map_get(node, "cond"))), hexa_str(")) {\n")));
        HexaVal wi = hexa_int(0);
        while (hexa_truthy(hexa_bool((wi).i < (hexa_int(hexa_len(hexa_map_get(node, "body")))).i))) {
            chunks = hexa_array_push(chunks, gen2_stmt(hexa_index_get(hexa_map_get(node, "body"), wi), hexa_add(depth, hexa_int(1))));
            wi = hexa_add(wi, hexa_int(1));
        }
        chunks = hexa_array_push(chunks, hexa_add(pad, hexa_str("}\n")));
        return hexa_str_join(chunks, hexa_str(""));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ForStmt")))) {
        HexaVal iter = hexa_map_get(node, "iter_expr");
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(iter), hexa_str("string"))))) && hexa_truthy(hexa_eq(hexa_map_get(iter, "kind"), hexa_str("Range")))))) {
            HexaVal s = gen2_expr(hexa_map_get(iter, "left"));
            HexaVal e = gen2_expr(hexa_map_get(iter, "right"));
            HexaVal chunks = hexa_array_new();
            chunks = hexa_array_push(chunks, hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(pad, hexa_str("for (HexaVal ")), hexa_map_get(node, "name")), hexa_str(" = ")), s), hexa_str("; ")), hexa_map_get(node, "name")), hexa_str(".i < (")), e), hexa_str(").i; ")), hexa_map_get(node, "name")), hexa_str(".i++) {\n")));
            HexaVal fi = hexa_int(0);
            while (hexa_truthy(hexa_bool((fi).i < (hexa_int(hexa_len(hexa_map_get(node, "body")))).i))) {
                chunks = hexa_array_push(chunks, gen2_stmt(hexa_index_get(hexa_map_get(node, "body"), fi), hexa_add(depth, hexa_int(1))));
                fi = hexa_add(fi, hexa_int(1));
            }
            chunks = hexa_array_push(chunks, hexa_add(pad, hexa_str("}\n")));
            return hexa_str_join(chunks, hexa_str(""));
        }
        HexaVal iter_c = gen2_expr(iter);
        HexaVal chunks = hexa_array_new();
        chunks = hexa_array_push(chunks, hexa_add(pad, hexa_str("{\n")));
        HexaVal pad2 = gen2_indent(hexa_add(depth, hexa_int(1)));
        chunks = hexa_array_push(chunks, hexa_add(hexa_add(hexa_add(pad2, hexa_str("HexaVal __iter_arr = ")), iter_c), hexa_str(";\n")));
        chunks = hexa_array_push(chunks, hexa_add(pad2, hexa_str("for (int __fi = 0; __fi < hexa_len(__iter_arr); __fi++) {\n")));
        HexaVal pad3 = gen2_indent(hexa_add(depth, hexa_int(2)));
        chunks = hexa_array_push(chunks, hexa_add(hexa_add(hexa_add(pad3, hexa_str("HexaVal ")), hexa_map_get(node, "name")), hexa_str(" = hexa_iter_get(__iter_arr, __fi);\n")));
        HexaVal fi = hexa_int(0);
        while (hexa_truthy(hexa_bool((fi).i < (hexa_int(hexa_len(hexa_map_get(node, "body")))).i))) {
            chunks = hexa_array_push(chunks, gen2_stmt(hexa_index_get(hexa_map_get(node, "body"), fi), hexa_add(depth, hexa_int(2))));
            fi = hexa_add(fi, hexa_int(1));
        }
        chunks = hexa_array_push(chunks, hexa_add(pad2, hexa_str("}\n")));
        chunks = hexa_array_push(chunks, hexa_add(pad, hexa_str("}\n")));
        return hexa_str_join(chunks, hexa_str(""));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("BreakStmt")))) {
        return hexa_add(pad, hexa_str("break;\n"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ContinueStmt")))) {
        return hexa_add(pad, hexa_str("continue;\n"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("ThrowStmt")))) {
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "left")), hexa_str("string")))))) {
            return hexa_add(hexa_add(hexa_add(pad, hexa_str("hexa_throw(")), gen2_expr(hexa_map_get(node, "left"))), hexa_str(");\n"));
        }
        return hexa_add(pad, hexa_str("hexa_throw(hexa_str(\"error\"));\n"));
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("TryCatchStmt"))) || hexa_truthy(hexa_eq(k, hexa_str("TryCatch")))))) {
        HexaVal try_body = hexa_map_get(node, "left");
        HexaVal catch_body = hexa_map_get(node, "right");
        HexaVal catch_var = hexa_map_get(node, "name");
        HexaVal chunks = hexa_array_new();
        chunks = hexa_array_push(chunks, hexa_add(pad, hexa_str("{\n")));
        HexaVal pad2 = gen2_indent(hexa_add(depth, hexa_int(1)));
        chunks = hexa_array_push(chunks, hexa_add(pad2, hexa_str("jmp_buf __jb; HexaVal __err = hexa_void();\n")));
        chunks = hexa_array_push(chunks, hexa_add(pad2, hexa_str("if (setjmp(__jb) == 0) {\n")));
        chunks = hexa_array_push(chunks, hexa_add(pad2, hexa_str("    __hexa_try_push(&__jb);\n")));
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(try_body), hexa_str("string")))))) {
            HexaVal ti = hexa_int(0);
            while (hexa_truthy(hexa_bool((ti).i < (hexa_int(hexa_len(try_body))).i))) {
                chunks = hexa_array_push(chunks, gen2_stmt(hexa_index_get(try_body, ti), hexa_add(depth, hexa_int(2))));
                ti = hexa_add(ti, hexa_int(1));
            }
        }
        chunks = hexa_array_push(chunks, hexa_add(pad2, hexa_str("    __hexa_try_pop();\n")));
        chunks = hexa_array_push(chunks, hexa_add(pad2, hexa_str("} else {\n")));
        chunks = hexa_array_push(chunks, hexa_add(pad2, hexa_str("    __err = __hexa_last_error();\n")));
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(catch_var, hexa_str(""))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(catch_var, hexa_str("_")))))))) {
            chunks = hexa_array_push(chunks, hexa_add(hexa_add(hexa_add(pad2, hexa_str("    HexaVal ")), catch_var), hexa_str(" = __err;\n")));
        }
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(catch_body), hexa_str("string")))))) {
            HexaVal ci = hexa_int(0);
            while (hexa_truthy(hexa_bool((ci).i < (hexa_int(hexa_len(catch_body))).i))) {
                chunks = hexa_array_push(chunks, gen2_stmt(hexa_index_get(catch_body, ci), hexa_add(depth, hexa_int(2))));
                ci = hexa_add(ci, hexa_int(1));
            }
        }
        chunks = hexa_array_push(chunks, hexa_add(pad2, hexa_str("}\n")));
        chunks = hexa_array_push(chunks, hexa_add(pad, hexa_str("}\n")));
        return hexa_str_join(chunks, hexa_str(""));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("MatchExpr")))) {
        return gen2_match_stmt(node, depth);
    }
    return hexa_add(hexa_add(hexa_add(pad, hexa_str("/* ")), k), hexa_str(" */\n"));
    return hexa_void();
}


HexaVal c_escape(HexaVal s) {
    HexaVal out = hexa_str_replace(hexa_to_string(s), hexa_str("\\"), hexa_str("\\\\"));
    out = hexa_str_replace(out, hexa_str("\""), hexa_str("\\\""));
    out = hexa_str_replace(out, hexa_str("\n"), hexa_str("\\n"));
    out = hexa_str_replace(out, hexa_str("\r"), hexa_str("\\r"));
    out = hexa_str_replace(out, hexa_str("\t"), hexa_str("\\t"));
    return out;
    return hexa_void();
}


HexaVal gen2_method_builtin(HexaVal obj_expr, HexaVal method, HexaVal args) {
    if (hexa_truthy(hexa_eq(method, hexa_str("push")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_push("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("len")))) {
        return hexa_add(hexa_add(hexa_str("hexa_int(hexa_len("), obj_expr), hexa_str("))"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("chars")))) {
        return hexa_add(hexa_add(hexa_str("hexa_str_chars("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("contains")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_str_contains("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str("))"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("join")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_str_join("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("to_upper")))) {
        return hexa_add(hexa_add(hexa_str("hexa_str_to_upper("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("to_lower")))) {
        return hexa_add(hexa_add(hexa_str("hexa_str_to_lower("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("split")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_str_split("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("trim")))) {
        return hexa_add(hexa_add(hexa_str("hexa_str_trim("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("replace")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_str_replace("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(1)))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("repeat")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_str_repeat("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("starts_with")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_str_starts_with("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str("))"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("ends_with")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_str_ends_with("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str("))"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("substring")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_str_substring("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(1)))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("index_of")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_int(hexa_str_index_of("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str("))"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("keys")))) {
        return hexa_add(hexa_add(hexa_str("hexa_map_keys("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("values")))) {
        return hexa_add(hexa_add(hexa_str("hexa_map_values("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("contains_key")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_map_contains_key("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(".s))"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("remove")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_map_remove("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(".s)"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("pop")))) {
        return hexa_add(hexa_add(hexa_str("hexa_array_pop("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("reverse")))) {
        return hexa_add(hexa_add(hexa_str("hexa_array_reverse("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("sort")))) {
        return hexa_add(hexa_add(hexa_str("hexa_array_sort("), obj_expr), hexa_str(")"));
    }
    // bt 55 — in-place array length shrink; caller must reassign.
    if (hexa_truthy(hexa_eq(method, hexa_str("truncate")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_truncate("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(")"));
    }
    // rt 32-B — scratch push (no stats) for reusable call_arg_buf; caller reassigns.
    if (hexa_truthy(hexa_eq(method, hexa_str("push_nostat")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_push_nostat("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(")"));
    }
    // rt 32-B — single-malloc+memcpy slice (counts as 1 array_new, not N pushes).
    if (hexa_truthy(hexa_eq(method, hexa_str("slice_fast")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_slice_fast("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(1)))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("parse_int")))) {
        return hexa_add(hexa_add(hexa_str("hexa_str_parse_int("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("parse_float")))) {
        return hexa_add(hexa_add(hexa_str("hexa_str_parse_float("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("trim_start")))) {
        return hexa_add(hexa_add(hexa_str("hexa_str_trim_start("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("trim_end")))) {
        return hexa_add(hexa_add(hexa_str("hexa_str_trim_end("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("bytes")))) {
        return hexa_add(hexa_add(hexa_str("hexa_str_bytes("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("pad_left")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_pad_left("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("pad_right")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_pad_right("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("to_string")))) {
        return hexa_add(hexa_add(hexa_str("hexa_to_string("), obj_expr), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("slice")))) {
        if (hexa_truthy(hexa_bool((hexa_int(hexa_len(args))).i >= (hexa_int(2)).i))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_slice("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(1)))), hexa_str(")"));
        }
        return hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_slice("), obj_expr), hexa_str(", ")), hexa_add(gen2_expr(hexa_index_get(args, hexa_int(0))), hexa_str(", hexa_int(0x7fffffff))")));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("map")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_map("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("filter")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_filter("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(method, hexa_str("fold")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_fold("), obj_expr), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(args, hexa_int(1)))), hexa_str(")"));
    }
    return hexa_add(hexa_add(hexa_str("hexa_void() /* unknown method."), method), hexa_str(" */"));
    return hexa_void();
}


HexaVal gen2_expr(HexaVal node) {
    HexaVal k = hexa_map_get(node, "kind");
    if (hexa_truthy(hexa_eq(k, hexa_str("IntLit")))) {
        return hexa_add(hexa_add(hexa_str("hexa_int("), hexa_map_get(node, "value")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("FloatLit")))) {
        return hexa_add(hexa_add(hexa_str("hexa_float("), hexa_map_get(node, "value")), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("BoolLit")))) {
        if (hexa_truthy(hexa_eq(hexa_map_get(node, "value"), hexa_str("true")))) {
            return hexa_str("hexa_bool(1)");
        }
        return hexa_str("hexa_bool(0)");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("StringLit")))) {
        return hexa_add(hexa_add(hexa_str("hexa_str(\""), c_escape(hexa_map_get(node, "value"))), hexa_str("\")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("CharLit")))) {
        return hexa_add(hexa_add(hexa_str("hexa_str(\""), c_escape(hexa_map_get(node, "value"))), hexa_str("\")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Ident")))) {
        /* bt-53: mangle reserved runtime names (TAG_INT etc.) so references
         * match the mangled global declarations emitted at LetStmt sites. */
        return hexa_mangle_ident(hexa_map_get(node, "name"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("BinOp")))) {
        HexaVal l = gen2_expr(hexa_map_get(node, "left"));
        HexaVal r = gen2_expr(hexa_map_get(node, "right"));
        HexaVal op = hexa_map_get(node, "op");
        if (hexa_truthy(hexa_eq(op, hexa_str("+")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_add("), l), hexa_str(", ")), r), hexa_str(")"));
        }
        /* T23 fix: route -, *, /, % through runtime helpers (hexa_sub/
         * hexa_mul/hexa_div/hexa_mod) which inspect the TAG_FLOAT tag
         * and use .f for float operands. The old code always read .i
         * from the HexaVal union, producing the raw bit pattern of the
         * double for float operands (garbage arithmetic). + already
         * used hexa_add so it was unaffected. */
        if (hexa_truthy(hexa_eq(op, hexa_str("-")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_sub("), l), hexa_str(", ")), r), hexa_str(")"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("*")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_mul("), l), hexa_str(", ")), r), hexa_str(")"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("/")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_div("), l), hexa_str(", ")), r), hexa_str(")"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("%")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_mod("), l), hexa_str(", ")), r), hexa_str(")"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("==")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_eq("), l), hexa_str(", ")), r), hexa_str(")"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("!=")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(!hexa_truthy(hexa_eq("), l), hexa_str(", ")), r), hexa_str(")))"));
        }
        /* T23 fix: ordered comparisons must honor TAG_FLOAT. The old
         * code read .i unconditionally which, for float operands,
         * compares the IEEE bit patterns as int64 — technically that
         * preserves order for same-sign positive floats but breaks for
         * mixed signs, int/float compares, and produces nonsensical
         * "false" results when the bit layout diverges. We emit a GCC
         * statement expression (already used by runtime.c hot macros)
         * that evaluates each side exactly once and promotes to double
         * when either operand is TAG_FLOAT. */
        if (hexa_truthy(hexa_eq(op, hexa_str("<")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(__extension__ ({ HexaVal __l=("), l), hexa_str("); HexaVal __r=(")), r), hexa_str("); (__l.tag==TAG_FLOAT||__r.tag==TAG_FLOAT) ? ((__l.tag==TAG_FLOAT?__l.f:(double)__l.i) < (__r.tag==TAG_FLOAT?__r.f:(double)__r.i)) : (__l.i < __r.i); }))"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str(">")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(__extension__ ({ HexaVal __l=("), l), hexa_str("); HexaVal __r=(")), r), hexa_str("); (__l.tag==TAG_FLOAT||__r.tag==TAG_FLOAT) ? ((__l.tag==TAG_FLOAT?__l.f:(double)__l.i) > (__r.tag==TAG_FLOAT?__r.f:(double)__r.i)) : (__l.i > __r.i); }))"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("<=")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(__extension__ ({ HexaVal __l=("), l), hexa_str("); HexaVal __r=(")), r), hexa_str("); (__l.tag==TAG_FLOAT||__r.tag==TAG_FLOAT) ? ((__l.tag==TAG_FLOAT?__l.f:(double)__l.i) <= (__r.tag==TAG_FLOAT?__r.f:(double)__r.i)) : (__l.i <= __r.i); }))"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str(">=")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(__extension__ ({ HexaVal __l=("), l), hexa_str("); HexaVal __r=(")), r), hexa_str("); (__l.tag==TAG_FLOAT||__r.tag==TAG_FLOAT) ? ((__l.tag==TAG_FLOAT?__l.f:(double)__l.i) >= (__r.tag==TAG_FLOAT?__r.f:(double)__r.i)) : (__l.i >= __r.i); }))"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("&&")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_truthy("), l), hexa_str(") && hexa_truthy(")), r), hexa_str("))"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("||")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_truthy("), l), hexa_str(") || hexa_truthy(")), r), hexa_str("))"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("??")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_null_coal("), l), hexa_str(", ")), r), hexa_str(")"));
        }
        // bt-68 fix: bitwise binops — parser already emits BitAnd/BitOr/BitXor
        // tokens with op values "&" / "|" / "^" (see parse_addition rt-35 patch).
        // Previously the codegen fell through to the placeholder comment,
        // producing empty C expressions (fatal syntax errors). Emit direct .i
        // bitwise ops; shifts `<<` / `>>` are future-proofed (parser lacks
        // shift tokens today but adding here is harmless).
        if (hexa_truthy(hexa_eq(op, hexa_str("&")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_int(("), l), hexa_str(").i & (")), r), hexa_str(").i)"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("|")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_int(("), l), hexa_str(").i | (")), r), hexa_str(").i)"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("^")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_int(("), l), hexa_str(").i ^ (")), r), hexa_str(").i)"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str("<<")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_int(("), l), hexa_str(").i << (")), r), hexa_str(").i)"));
        }
        if (hexa_truthy(hexa_eq(op, hexa_str(">>")))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_int(("), l), hexa_str(").i >> (")), r), hexa_str(").i)"));
        }
        return hexa_add(hexa_add(hexa_str("/* binop "), op), hexa_str(" */"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("UnaryOp")))) {
        /* T23 fix: unary minus must honor TAG_FLOAT. Use hexa_sub(0, x)
         * which routes through the float-aware runtime helper. */
        if (hexa_truthy(hexa_eq(hexa_map_get(node, "op"), hexa_str("-")))) {
            return hexa_add(hexa_add(hexa_str("hexa_sub(hexa_int(0), "), gen2_expr(hexa_map_get(node, "left"))), hexa_str(")"));
        }
        if (hexa_truthy(hexa_eq(hexa_map_get(node, "op"), hexa_str("!")))) {
            return hexa_add(hexa_add(hexa_str("hexa_bool(!hexa_truthy("), gen2_expr(hexa_map_get(node, "left"))), hexa_str("))"));
        }
        if (hexa_truthy(hexa_eq(hexa_map_get(node, "op"), hexa_str("~")))) {
            return hexa_add(hexa_add(hexa_str("hexa_int(~("), gen2_expr(hexa_map_get(node, "left"))), hexa_str(").i)"));
        }
        return hexa_add(hexa_add(hexa_str("/* unary "), hexa_map_get(node, "op")), hexa_str(" */"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Field")))) {
        HexaVal obj = gen2_expr(hexa_map_get(node, "left"));
        HexaVal field = hexa_map_get(node, "name");
        /* rt#37: emit hexa_map_get_ic with a unique static HexaIC slot per
         * call site. Monomorphic sites converge to ~100% hit rate after the
         * first call; we skip the strcmp-chain on every hit.
         * HEXA_IC_DISABLE=1 at codegen time falls back to plain hexa_map_get
         * for A/B benchmarking. */
        const char* ic_disable = getenv("HEXA_IC_DISABLE");
        if (ic_disable && ic_disable[0] == '1') {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_map_get("), obj), hexa_str(", \"")), field), hexa_str("\")"));
        }
        static int _hexa_ic_counter = 0;
        int id = _hexa_ic_counter++;
        char slot_buf[48];
        snprintf(slot_buf, sizeof(slot_buf), "__hexa_ic_%d", id);
        HexaVal slot = hexa_str(slot_buf);
        extern HexaVal _ic_def_parts;
        extern int _ic_def_parts_init;
        if (!_ic_def_parts_init) { _ic_def_parts = hexa_array_new(); _ic_def_parts_init = 1; }
        HexaVal def_line = hexa_add(hexa_add(hexa_str("static HexaIC "), slot), hexa_str(" = {0};\n"));
        _ic_def_parts = hexa_array_push(_ic_def_parts, def_line);
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_map_get_ic("), obj), hexa_str(", \"")), field), hexa_str("\", &")), slot), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("StructInit")))) {
        HexaVal name = hexa_map_get(node, "name");
        HexaVal arg_strs = hexa_array_new();
        HexaVal si = hexa_int(0);
        while (hexa_truthy(hexa_bool((si).i < (hexa_int(hexa_len(hexa_map_get(node, "fields")))).i))) {
            arg_strs = hexa_array_push(arg_strs, gen2_expr(hexa_map_get(hexa_index_get(hexa_map_get(node, "fields"), si), "left")));
            si = hexa_add(si, hexa_int(1));
        }
        return hexa_add(hexa_add(hexa_add(name, hexa_str("(")), hexa_str_join(arg_strs, hexa_str(", "))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Call")))) {
        HexaVal callee = hexa_map_get(node, "left");
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(callee), hexa_str("string"))))) && hexa_truthy(hexa_eq(hexa_map_get(callee, "kind"), hexa_str("Ident")))))) {
            HexaVal name = hexa_map_get(callee, "name");
            if (hexa_truthy(hexa_eq(name, hexa_str("println")))) {
                if (hexa_truthy(hexa_bool((hexa_int(hexa_len(hexa_map_get(node, "args")))).i > (hexa_int(0)).i))) {
                    return hexa_add(hexa_add(hexa_str("hexa_println("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
                }
                return hexa_str("(printf(\"\\n\"), hexa_void())");
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("print")))) {
                if (hexa_truthy(hexa_bool((hexa_int(hexa_len(hexa_map_get(node, "args")))).i > (hexa_int(0)).i))) {
                    return hexa_add(hexa_add(hexa_str("(hexa_print_val("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str("), hexa_void())"));
                }
                return hexa_str("hexa_void()");
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("len")))) {
                return hexa_add(hexa_add(hexa_str("hexa_int(hexa_len("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str("))"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("to_string")))) {
                return hexa_add(hexa_add(hexa_str("hexa_to_string("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("type_of")))) {
                return hexa_add(hexa_add(hexa_str("hexa_type_of("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("sqrt")))) {
                return hexa_add(hexa_add(hexa_str("hexa_sqrt("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("floor")))) {
                return hexa_add(hexa_add(hexa_str("hexa_floor("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ceil")))) {
                return hexa_add(hexa_add(hexa_str("hexa_ceil("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("abs")))) {
                return hexa_add(hexa_add(hexa_str("hexa_abs("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("round")))) {
                /* T32 pattern fix: unwrap TAG_VALSTRUCT via __hx_to_double */
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_str("hexa_int((int64_t)round(__hx_to_double("), a), hexa_str(")))"));
            }
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(name, hexa_str("ln"))) || hexa_truthy(hexa_eq(name, hexa_str("log")))))) {
                /* T32 pattern fix: unwrap TAG_VALSTRUCT via __hx_to_double */
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_str("hexa_float(log(__hx_to_double("), a), hexa_str(")))"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("log10")))) {
                /* T32 pattern fix: unwrap TAG_VALSTRUCT via __hx_to_double */
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_str("hexa_float(log10(__hx_to_double("), a), hexa_str(")))"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("exp")))) {
                /* T32 pattern fix: unwrap TAG_VALSTRUCT via __hx_to_double */
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_str("hexa_float(exp(__hx_to_double("), a), hexa_str(")))"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("sin")))) {
                /* T32 pattern fix: unwrap TAG_VALSTRUCT via __hx_to_double */
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_str("hexa_float(sin(__hx_to_double("), a), hexa_str(")))"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("cos")))) {
                /* T32 pattern fix: unwrap TAG_VALSTRUCT via __hx_to_double */
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_str("hexa_float(cos(__hx_to_double("), a), hexa_str(")))"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("pow")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_pow("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("to_float")))) {
                /* T32 fix: old inline `v.tag==TAG_FLOAT?v.f:(double)v.i`
                 * read only the outer HexaVal tag, so interpreter values
                 * (TAG_VALSTRUCT) fell through and cast the vs pointer
                 * bits to double. __hx_to_double (self/runtime.c) unwraps
                 * TAG_VALSTRUCT via vs->tag_i / vs->float_val / vs->int_val. */
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_str("hexa_float(__hx_to_double("), a), hexa_str("))"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("is_alpha")))) {
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(("), a), hexa_str(".tag==TAG_STR && ")), a), hexa_str(".s && isalpha((unsigned char)")), a), hexa_str(".s[0])) || (")), a), hexa_str(".tag==TAG_CHAR && isalpha((unsigned char)")), a), hexa_str(".i)))"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("is_digit")))) {
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(("), a), hexa_str(".tag==TAG_STR && ")), a), hexa_str(".s && isdigit((unsigned char)")), a), hexa_str(".s[0])) || (")), a), hexa_str(".tag==TAG_CHAR && isdigit((unsigned char)")), a), hexa_str(".i)))"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("is_alphanumeric")))) {
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(("), a), hexa_str(".tag==TAG_STR && ")), a), hexa_str(".s && isalnum((unsigned char)")), a), hexa_str(".s[0])) || (")), a), hexa_str(".tag==TAG_CHAR && isalnum((unsigned char)")), a), hexa_str(".i)))"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("file_exists")))) {
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_str("hexa_bool(access("), a), hexa_str(".s, F_OK) == 0)"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("char_code")))) {
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                HexaVal b = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)));
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_int((unsigned char)"), a), hexa_str(".s[(int)")), b), hexa_str(".i])"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("format")))) {
                HexaVal fmt_args = hexa_str("hexa_array_new()");
                HexaVal fi = hexa_int(1);
                while (hexa_truthy(hexa_bool((fi).i < (hexa_int(hexa_len(hexa_map_get(node, "args")))).i))) {
                    fmt_args = hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_push("), fmt_args), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), fi))), hexa_str(")"));
                    fi = hexa_add(fi, hexa_int(1));
                }
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_format_n("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), fmt_args), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("eprintln")))) {
                if (hexa_truthy(hexa_bool((hexa_int(hexa_len(hexa_map_get(node, "args")))).i > (hexa_int(0)).i))) {
                    return hexa_add(hexa_add(hexa_str("(hexa_eprint_val("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str("), fprintf(stderr, \"\\n\"), hexa_void())"));
                }
                return hexa_str("(fprintf(stderr, \"\\n\"), hexa_void())");
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("args")))) {
                return hexa_str("hexa_args()");
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("exec")))) {
                return hexa_add(hexa_add(hexa_str("hexa_exec("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("to_int")))) {
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_int(("), a), hexa_str(".tag==TAG_FLOAT?(int64_t)")), a), hexa_str(".f:")), a), hexa_str(".tag==TAG_STR?atoll(")), a), hexa_str(".s):")), a), hexa_str(".i))"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("min")))) {
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                HexaVal b = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)));
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_int(("), a), hexa_str(").i < (")), b), hexa_str(").i ? (")), a), hexa_str(").i : (")), b), hexa_str(").i)"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("max")))) {
                HexaVal a = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                HexaVal b = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)));
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_int(("), a), hexa_str(").i > (")), b), hexa_str(").i ? (")), a), hexa_str(").i : (")), b), hexa_str(").i)"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("read_file")))) {
                return hexa_add(hexa_add(hexa_str("hexa_read_file("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("write_file")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_write_file("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("cstring")))) {
                return hexa_add(hexa_add(hexa_str("hexa_cstring("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("from_cstring")))) {
                return hexa_add(hexa_add(hexa_str("hexa_from_cstring("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_null")))) {
                return hexa_str("hexa_ptr_null()");
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_addr")))) {
                return hexa_add(hexa_add(hexa_str("hexa_ptr_addr("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("callback_create")))) {
                HexaVal arg = hexa_index_get(hexa_map_get(node, "args"), hexa_int(0));
                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(arg), hexa_str("string"))))) && hexa_truthy(hexa_eq(hexa_map_get(arg, "kind"), hexa_str("Ident")))))) {
                    HexaVal fn_name = hexa_map_get(arg, "name");
                    return hexa_add(hexa_add(hexa_str("hexa_callback_create((HexaVal){.tag=TAG_FN, .fn={.fn_ptr=(void*)"), fn_name), hexa_str(", .arity=0}})"));
                }
                return hexa_add(hexa_add(hexa_str("hexa_callback_create("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("callback_free")))) {
                return hexa_add(hexa_add(hexa_str("hexa_callback_free("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("callback_slot_id")))) {
                return hexa_add(hexa_add(hexa_str("hexa_callback_slot_id("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_alloc")))) {
                return hexa_add(hexa_add(hexa_str("hexa_ptr_alloc("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_free")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_ptr_free("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_write")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_ptr_write("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(2)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_write_f32")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_ptr_write_f32("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(2)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_write_i32")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_ptr_write_i32("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(2)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_read")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_ptr_read("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_read_f64")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_ptr_read_f64("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_read_f32")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_ptr_read_f32("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_read_i32")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_ptr_read_i32("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("ptr_offset")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_ptr_offset("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("deref")))) {
                return hexa_add(hexa_add(hexa_str("hexa_deref("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("struct_pack")))) {
                HexaVal a_strs = hexa_array_new();
                HexaVal si = hexa_int(0);
                while (hexa_truthy(hexa_bool((si).i < (hexa_int(hexa_len(hexa_map_get(node, "args")))).i))) {
                    a_strs = hexa_array_push(a_strs, gen2_expr(hexa_index_get(hexa_map_get(node, "args"), si)));
                    si = hexa_add(si, hexa_int(1));
                }
                HexaVal n = hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "args"))));
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_struct_pack_map((HexaVal[]){"), hexa_str_join(a_strs, hexa_str(", "))), hexa_str("}, ")), n), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("struct_pack_f32")))) {
                HexaVal a_strs = hexa_array_new();
                HexaVal si = hexa_int(0);
                while (hexa_truthy(hexa_bool((si).i < (hexa_int(hexa_len(hexa_map_get(node, "args")))).i))) {
                    a_strs = hexa_array_push(a_strs, gen2_expr(hexa_index_get(hexa_map_get(node, "args"), si)));
                    si = hexa_add(si, hexa_int(1));
                }
                HexaVal n = hexa_to_string(hexa_int(hexa_len(hexa_map_get(node, "args"))));
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_struct_pack_f32((HexaVal[]){"), hexa_str_join(a_strs, hexa_str(", "))), hexa_str("}, ")), n), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("struct_unpack")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_struct_unpack("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("struct_unpack_f32")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_struct_unpack_f32("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("struct_rect")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_struct_rect("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(2)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(3)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("struct_point")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_struct_point("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("struct_size")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_struct_size_pack("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(name, hexa_str("struct_free")))) {
                return hexa_add(hexa_add(hexa_str("hexa_struct_free("), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            // ─── bt-66 fix: free-fn call of a builtin method name ──────────────────
            // source:  contains(attrs, "memoize")   (bare ident, not a.method form)
            // previous codegen emitted:  hexa_call2(contains, attrs, ...)
            // which references `contains` as a C symbol — undefined → link fail.
            // If `name` is a known string-method builtin and not a user global,
            // rewrite to the receiver-first builtin dispatch (args[0] = receiver).
            if (hexa_truthy(hexa_bool(!hexa_truthy(_is_known_fn_global(name))))) {
                HexaVal nargs_mr = hexa_int(hexa_len(hexa_map_get(node, "args")));
                if (hexa_truthy(hexa_bool((nargs_mr).i >= (hexa_int(2)).i))) {
                    HexaVal recv_mr = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                    HexaVal arg1_mr = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)));
                    if (hexa_truthy(hexa_eq(name, hexa_str("contains")))) {
                        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_str_contains("), recv_mr), hexa_str(", ")), arg1_mr), hexa_str("))"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("starts_with")))) {
                        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_str_starts_with("), recv_mr), hexa_str(", ")), arg1_mr), hexa_str("))"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("ends_with")))) {
                        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_str_ends_with("), recv_mr), hexa_str(", ")), arg1_mr), hexa_str("))"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("join")))) {
                        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_str_join("), recv_mr), hexa_str(", ")), arg1_mr), hexa_str(")"));
                    }
                }
            }
            // ─── bt-71 fix: libc / builtin family dispatch ─────────────────────────
            if (hexa_truthy(hexa_bool(!hexa_truthy(_is_known_fn_global(name))))) {
                HexaVal n71 = hexa_int(hexa_len(hexa_map_get(node, "args")));
                if (hexa_truthy(hexa_eq(n71, hexa_int(0)))) {
                    if (hexa_truthy(hexa_eq(name, hexa_str("timestamp")))) {
                        return hexa_str("hexa_timestamp()");
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("input")))) {
                        return hexa_str("hexa_input(hexa_str(\"\"))");
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("random")))) {
                        return hexa_str("hexa_random()");
                    }
                }
                if (hexa_truthy(hexa_eq(n71, hexa_int(1)))) {
                    HexaVal a0 = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                    if (hexa_truthy(hexa_eq(name, hexa_str("exit")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_exit("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("sleep")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_sleep("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("tanh")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_math_tanh("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("sin")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_math_sin("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("cos")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_math_cos("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("tan")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_math_tan("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("asin")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_math_asin("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("acos")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_math_acos("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("atan")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_math_atan("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("log")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_math_log("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("exp")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_math_exp("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("input")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_input("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("is_error")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_is_error("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("read_lines")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_read_lines("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("from_char_code")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_from_char_code("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("env_var")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_env_var("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("env")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_env_var("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("delete_file")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_delete_file("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("bin")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_bin("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("hex")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_hex("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("base64_encode")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_base64_encode("), a0), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("base64_decode")))) {
                        return hexa_add(hexa_add(hexa_str("hexa_base64_decode("), a0), hexa_str(")"));
                    }
                }
                if (hexa_truthy(hexa_eq(n71, hexa_int(2)))) {
                    HexaVal b0 = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)));
                    HexaVal b1 = gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)));
                    if (hexa_truthy(hexa_eq(name, hexa_str("append_file")))) {
                        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_append_file("), b0), hexa_str(", ")), b1), hexa_str(")"));
                    }
                    if (hexa_truthy(hexa_eq(name, hexa_str("atan2")))) {
                        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_math_atan2("), b0), hexa_str(", ")), b1), hexa_str(")"));
                    }
                }
            }
            HexaVal arg_strs = hexa_array_new();
            HexaVal ai = hexa_int(0);
            while (hexa_truthy(hexa_bool((ai).i < (hexa_int(hexa_len(hexa_map_get(node, "args")))).i))) {
                // rt#32-O: snapshot-wrap each arg to insulate arena-backed
                // arrays from peer args that may rewind arena marks.
                HexaVal _arg_node = hexa_index_get(hexa_map_get(node, "args"), ai);
                HexaVal _arg_c = gen2_expr(_arg_node);
                _arg_c = _gen2_maybe_snapshot_arg(_arg_c, _arg_node);
                arg_strs = hexa_array_push(arg_strs, _arg_c);
                ai = hexa_add(ai, hexa_int(1));
            }
            if (hexa_truthy(_is_known_fn_global(name))) {
                return hexa_add(hexa_add(hexa_add(hexa_mangle_ident(name), hexa_str("(")), hexa_str_join(arg_strs, hexa_str(", "))), hexa_str(")"));
            }
            HexaVal nargs = hexa_int(hexa_len(hexa_map_get(node, "args")));
            if (hexa_truthy(hexa_eq(nargs, hexa_int(0)))) {
                return hexa_add(hexa_add(hexa_str("hexa_call0("), name), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(nargs, hexa_int(1)))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_call1("), name), hexa_str(", ")), hexa_index_get(arg_strs, hexa_int(0))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(nargs, hexa_int(2)))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_call2("), name), hexa_str(", ")), hexa_str_join(arg_strs, hexa_str(", "))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(nargs, hexa_int(3)))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_call3("), name), hexa_str(", ")), hexa_str_join(arg_strs, hexa_str(", "))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(nargs, hexa_int(4)))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_call4("), name), hexa_str(", ")), hexa_str_join(arg_strs, hexa_str(", "))), hexa_str(")"));
            }
            return hexa_add(hexa_add(hexa_add(name, hexa_str("(")), hexa_str_join(arg_strs, hexa_str(", "))), hexa_str(")"));
        }
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(callee), hexa_str("string"))))) && hexa_truthy(hexa_eq(hexa_map_get(callee, "kind"), hexa_str("Field")))))) {
            HexaVal obj = gen2_expr(hexa_map_get(callee, "left"));
            HexaVal method = hexa_map_get(callee, "name");
            HexaVal user_types = _method_registry_lookup(method);
            if (hexa_truthy(hexa_bool((hexa_int(hexa_len(user_types))).i > (hexa_int(0)).i))) {
                HexaVal u_args = hexa_array_new();
                HexaVal uai = hexa_int(0);
                while (hexa_truthy(hexa_bool((uai).i < (hexa_int(hexa_len(hexa_map_get(node, "args")))).i))) {
                    u_args = hexa_array_push(u_args, gen2_expr(hexa_index_get(hexa_map_get(node, "args"), uai)));
                    uai = hexa_add(uai, hexa_int(1));
                }
                HexaVal u_arg_tail = hexa_str_join(u_args, hexa_str(", "));
                HexaVal __saved_obj = obj;
                HexaVal fb = gen2_method_builtin(hexa_str("__mr"), method, hexa_map_get(node, "args"));
                HexaVal chain = fb;
                HexaVal ti = hexa_int((hexa_int(hexa_len(user_types))).i - (hexa_int(1)).i);
                while (hexa_truthy(hexa_bool((ti).i >= (hexa_int(0)).i))) {
                    HexaVal tname = hexa_index_get(user_types, ti);
                    HexaVal call_args = hexa_str("__mr");
                    HexaVal full_args = (hexa_truthy(hexa_eq(u_arg_tail, hexa_str(""))) ? call_args : hexa_add(hexa_add(call_args, hexa_str(", ")), u_arg_tail));
                    chain = hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("(hexa_is_type(__mr, \""), tname), hexa_str("\") ? ")), tname), hexa_str("__")), method), hexa_str("(")), full_args), hexa_str(") : ")), chain), hexa_str(")"));
                    ti = hexa_int((ti).i - (hexa_int(1)).i);
                }
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("({ HexaVal __mr = "), __saved_obj), hexa_str("; ")), chain), hexa_str("; })"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("push")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_push("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("len")))) {
                return hexa_add(hexa_add(hexa_str("hexa_int(hexa_len("), obj), hexa_str("))"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("chars")))) {
                return hexa_add(hexa_add(hexa_str("hexa_str_chars("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("contains")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_str_contains("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str("))"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("join")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_str_join("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("to_upper")))) {
                return hexa_add(hexa_add(hexa_str("hexa_str_to_upper("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("to_lower")))) {
                return hexa_add(hexa_add(hexa_str("hexa_str_to_lower("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("split")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_str_split("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("trim")))) {
                return hexa_add(hexa_add(hexa_str("hexa_str_trim("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("replace")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_str_replace("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("repeat")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_str_repeat("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("starts_with")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_str_starts_with("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str("))"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("ends_with")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_str_ends_with("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str("))"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("substring")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_str_substring("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("index_of")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_int(hexa_str_index_of("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str("))"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("keys")))) {
                return hexa_add(hexa_add(hexa_str("hexa_map_keys("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("values")))) {
                return hexa_add(hexa_add(hexa_str("hexa_map_values("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("contains_key")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_bool(hexa_map_contains_key("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(".s))"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("remove")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_map_remove("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(".s)"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("pop")))) {
                return hexa_add(hexa_add(hexa_str("hexa_array_pop("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("reverse")))) {
                return hexa_add(hexa_add(hexa_str("hexa_array_reverse("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("sort")))) {
                return hexa_add(hexa_add(hexa_str("hexa_array_sort("), obj), hexa_str(")"));
            }
            // bt 55 — in-place array length shrink; caller must reassign.
            if (hexa_truthy(hexa_eq(method, hexa_str("truncate")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_truncate("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            // rt 32-B — scratch push (no stats) for reusable call_arg_buf; caller reassigns.
            if (hexa_truthy(hexa_eq(method, hexa_str("push_nostat")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_push_nostat("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            // rt 32-B — single-malloc+memcpy slice (counts as 1 array_new, not N pushes).
            if (hexa_truthy(hexa_eq(method, hexa_str("slice_fast")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_slice_fast("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("parse_int")))) {
                return hexa_add(hexa_add(hexa_str("hexa_str_parse_int("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("parse_float")))) {
                return hexa_add(hexa_add(hexa_str("hexa_str_parse_float("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("trim_start")))) {
                return hexa_add(hexa_add(hexa_str("hexa_str_trim_start("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("trim_end")))) {
                return hexa_add(hexa_add(hexa_str("hexa_str_trim_end("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("bytes")))) {
                return hexa_add(hexa_add(hexa_str("hexa_str_bytes("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("pad_left")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_pad_left("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("pad_right")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_pad_right("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("to_string")))) {
                return hexa_add(hexa_add(hexa_str("hexa_to_string("), obj), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("slice")))) {
                if (hexa_truthy(hexa_bool((hexa_int(hexa_len(hexa_map_get(node, "args")))).i >= (hexa_int(2)).i))) {
                    return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_slice("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
                }
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_slice("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", hexa_int(0x7fffffff))")), hexa_str(""));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("map")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_map("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("filter")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_filter("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(")"));
            }
            if (hexa_truthy(hexa_eq(method, hexa_str("fold")))) {
                return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_fold("), obj), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(0)))), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "args"), hexa_int(1)))), hexa_str(")"));
            }
            return hexa_add(hexa_add(hexa_str("/* method."), method), hexa_str(" */"));
        }
        HexaVal cexpr = gen2_expr(callee);
        HexaVal nargs = hexa_int(hexa_len(hexa_map_get(node, "args")));
        HexaVal cargs = hexa_array_new();
        HexaVal ci = hexa_int(0);
        while (hexa_truthy(hexa_bool((ci).i < (nargs).i))) {
            // rt#32-O: snapshot-wrap each arg (indirect call path).
            HexaVal _arg_node = hexa_index_get(hexa_map_get(node, "args"), ci);
            HexaVal _arg_c = gen2_expr(_arg_node);
            _arg_c = _gen2_maybe_snapshot_arg(_arg_c, _arg_node);
            cargs = hexa_array_push(cargs, _arg_c);
            ci = hexa_add(ci, hexa_int(1));
        }
        if (hexa_truthy(hexa_eq(nargs, hexa_int(0)))) {
            return hexa_add(hexa_add(hexa_str("hexa_call0("), cexpr), hexa_str(")"));
        }
        if (hexa_truthy(hexa_eq(nargs, hexa_int(1)))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_call1("), cexpr), hexa_str(", ")), hexa_index_get(cargs, hexa_int(0))), hexa_str(")"));
        }
        if (hexa_truthy(hexa_eq(nargs, hexa_int(2)))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_call2("), cexpr), hexa_str(", ")), hexa_str_join(cargs, hexa_str(", "))), hexa_str(")"));
        }
        if (hexa_truthy(hexa_eq(nargs, hexa_int(3)))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_call3("), cexpr), hexa_str(", ")), hexa_str_join(cargs, hexa_str(", "))), hexa_str(")"));
        }
        if (hexa_truthy(hexa_eq(nargs, hexa_int(4)))) {
            return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_call4("), cexpr), hexa_str(", ")), hexa_str_join(cargs, hexa_str(", "))), hexa_str(")"));
        }
        return hexa_add(hexa_add(hexa_str("/* indirect call arity "), hexa_to_string(nargs)), hexa_str(" unsupported */"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Array")))) {
        HexaVal out = hexa_str("hexa_array_new()");
        HexaVal ai = hexa_int(0);
        while (hexa_truthy(hexa_bool((ai).i < (hexa_int(hexa_len(hexa_map_get(node, "items")))).i))) {
            out = hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_push("), out), hexa_str(", ")), gen2_expr(hexa_index_get(hexa_map_get(node, "items"), ai))), hexa_str(")"));
            ai = hexa_add(ai, hexa_int(1));
        }
        return out;
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Index")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_index_get("), gen2_expr(hexa_map_get(node, "left"))), hexa_str(", ")), gen2_expr(hexa_map_get(node, "right"))), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("EnumPath")))) {
        HexaVal ename = hexa_map_get(node, "name");
        HexaVal vname = hexa_map_get(node, "value");
        return hexa_add(hexa_add(ename, hexa_str("_")), vname);
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("IfExpr")))) {
        HexaVal c = gen2_expr(hexa_map_get(node, "cond"));
        HexaVal t_val = hexa_str("hexa_void()");
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "then_body")), hexa_str("string"))))) && hexa_truthy(hexa_bool((hexa_int(hexa_len(hexa_map_get(node, "then_body")))).i > (hexa_int(0)).i))))) {
            HexaVal t_last = hexa_index_get(hexa_map_get(node, "then_body"), hexa_int((hexa_int(hexa_len(hexa_map_get(node, "then_body")))).i - (hexa_int(1)).i));
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(t_last), hexa_str("string"))))) && hexa_truthy(hexa_eq(hexa_map_get(t_last, "kind"), hexa_str("ExprStmt"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(t_last, "left")), hexa_str("string")))))))) {
                t_val = gen2_expr(hexa_map_get(t_last, "left"));
            }
        }
        HexaVal e_val = hexa_str("hexa_void()");
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "else_body")), hexa_str("string"))))) && hexa_truthy(hexa_bool((hexa_int(hexa_len(hexa_map_get(node, "else_body")))).i > (hexa_int(0)).i))))) {
            HexaVal e_last = hexa_index_get(hexa_map_get(node, "else_body"), hexa_int((hexa_int(hexa_len(hexa_map_get(node, "else_body")))).i - (hexa_int(1)).i));
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(e_last), hexa_str("string"))))) && hexa_truthy(hexa_eq(hexa_map_get(e_last, "kind"), hexa_str("ExprStmt"))))) && hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(e_last, "left")), hexa_str("string")))))))) {
                e_val = gen2_expr(hexa_map_get(e_last, "left"));
            }
        }
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("(hexa_truthy("), c), hexa_str(") ? ")), t_val), hexa_str(" : ")), e_val), hexa_str(")"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("MatchExpr")))) {
        return gen2_match_expr(node);
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Lambda")))) {
        return gen2_lambda_expr(node);
    }
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("Tuple"))) || hexa_truthy(hexa_eq(k, hexa_str("TupleLit")))))) {
        HexaVal out = hexa_str("hexa_array_new()");
        HexaVal items = hexa_map_get(node, "items");
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(items), hexa_str("string")))))) {
            HexaVal ti = hexa_int(0);
            while (hexa_truthy(hexa_bool((ti).i < (hexa_int(hexa_len(items))).i))) {
                out = hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_push("), out), hexa_str(", ")), gen2_expr(hexa_index_get(items, ti))), hexa_str(")"));
                ti = hexa_add(ti, hexa_int(1));
            }
        }
        return out;
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("MapLit")))) {
        // rt 32-A: MapLit fast-path — if every key is a StringLit, emit a
        // single hexa_struct_pack_map call using C99 compound literals.
        // This collapses N chained hexa_map_set calls into 1 bulk op.
        // Fallback to the old chained hexa_map_set for non-literal keys.
        HexaVal items = hexa_map_get(node, "items");
        if (hexa_truthy(hexa_eq(hexa_type_of(items), hexa_str("string")))) {
            return hexa_str("hexa_map_new()");
        }
        HexaVal nitems = hexa_int(hexa_len(items));
        if (hexa_truthy(hexa_eq(nitems, hexa_int(0)))) {
            return hexa_str("hexa_map_new()");
        }
        // Scan: are all keys StringLit?
        HexaVal mi = hexa_int(0);
        HexaVal all_lit = hexa_bool(1);
        while (hexa_truthy(hexa_bool((mi).i < (nitems).i))) {
            HexaVal entry = hexa_index_get(items, mi);
            HexaVal left = hexa_map_get(entry, "left");
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(hexa_type_of(left), hexa_str("string"))) || !hexa_truthy(hexa_eq(hexa_map_get(left, "kind"), hexa_str("StringLit")))))) {
                all_lit = hexa_bool(0);
                break;
            }
            mi = hexa_add(mi, hexa_int(1));
        }
        if (hexa_truthy(all_lit)) {
            HexaVal keys_s = hexa_array_new();
            HexaVal vals_s = hexa_array_new();
            HexaVal j = hexa_int(0);
            while (hexa_truthy(hexa_bool((j).i < (nitems).i))) {
                HexaVal e = hexa_index_get(items, j);
                if (hexa_truthy(hexa_bool((j).i > (hexa_int(0)).i))) {
                    keys_s = hexa_array_push(keys_s, hexa_str(", "));
                    vals_s = hexa_array_push(vals_s, hexa_str(", "));
                }
                keys_s = hexa_array_push(keys_s, hexa_add(hexa_add(hexa_str("\""), c_escape(hexa_map_get(hexa_map_get(e, "left"), "value"))), hexa_str("\"")));
                vals_s = hexa_array_push(vals_s, gen2_expr(hexa_map_get(e, "right")));
                j = hexa_add(j, hexa_int(1));
            }
            HexaVal nstr = hexa_to_string(nitems);
            HexaVal r = hexa_str("hexa_struct_pack_map(\"\", ");
            r = hexa_add(r, nstr);
            r = hexa_add(r, hexa_str(", (const char*[]){"));
            r = hexa_add(r, hexa_str_join(keys_s, hexa_str("")));
            r = hexa_add(r, hexa_str("}, (HexaVal[]){"));
            r = hexa_add(r, hexa_str_join(vals_s, hexa_str("")));
            r = hexa_add(r, hexa_str("})"));
            return r;
        }
        // Fallback: chained hexa_map_set (non-literal keys).
        HexaVal out = hexa_str("hexa_map_new()");
        HexaVal mi2 = hexa_int(0);
        while (hexa_truthy(hexa_bool((mi2).i < (nitems).i))) {
            HexaVal entry = hexa_index_get(items, mi2);
            HexaVal key_c = gen2_expr(hexa_map_get(entry, "left"));
            if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(entry, "left")), hexa_str("string"))))) && hexa_truthy(hexa_eq(hexa_map_get(hexa_map_get(entry, "left"), "kind"), hexa_str("StringLit")))))) {
                key_c = hexa_add(hexa_add(hexa_str("\""), c_escape(hexa_map_get(hexa_map_get(entry, "left"), "value"))), hexa_str("\""));
            }
            out = hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_map_set("), out), hexa_str(", ")), key_c), hexa_str(", ")), gen2_expr(hexa_map_get(entry, "right"))), hexa_str(")"));
            mi2 = hexa_add(mi2, hexa_int(1));
        }
        return out;
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("NegFloat")))) {
        return hexa_add(hexa_add(hexa_str("hexa_float(-("), gen2_expr(hexa_map_get(node, "left"))), hexa_str(").f)"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Wildcard")))) {
        return hexa_str("/* wildcard */");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("")))) {
        return hexa_str("hexa_map_new()");
    }
    return hexa_add(hexa_add(hexa_str("/* expr:"), k), hexa_str(" */"));
    return hexa_void();
}


HexaVal gen2_struct_decl(HexaVal node) {
    // rt 32-A: struct constructor fast-path.  Instead of emitting:
    //     HexaVal __s = hexa_map_new();
    //     __s = hexa_map_set(__s, "__type__", ...);
    //     __s = hexa_map_set(__s, "field_i", field_i);  // ×N
    // we now emit a single bulk call:
    //     static const char* _k[] = {"f0","f1",...};
    //     HexaVal _v[] = {f0, f1, ...};
    //     return hexa_struct_pack_map("Name", N, _k, _v);
    // which pre-sizes the hash table once (no rehash) and batch-inserts.
    // For Val with 12 fields this collapses 14 alloc-counter bumps to 1.
    //
    // rt 32-G Phase 0: further specialize `Val` (12 fields, ~3.37M
    // constructions on d64 200-step) → hexa_valstruct_new_v flat struct.
    // Zero hash-table involvement; one malloc per Val construction.
    HexaVal name = hexa_map_get(node, "name");
    // Val special case: emit flat-struct constructor.
    if (name.tag == TAG_STR && strcmp(name.s, "Val") == 0) {
        HexaVal fields = hexa_map_get(node, "fields");
        int nf = (int)hexa_len(fields);
        if (nf == 12) {
            HexaVal params_v = hexa_array_new();
            HexaVal args_v = hexa_array_new();
            for (int k = 0; k < nf; k++) {
                HexaVal f = hexa_index_get(fields, hexa_int(k));
                HexaVal fname = hexa_map_get(f, "name");
                if (k > 0) {
                    params_v = hexa_array_push(params_v, hexa_str(", "));
                    args_v = hexa_array_push(args_v, hexa_str(", "));
                }
                params_v = hexa_array_push(params_v, hexa_add(hexa_str("HexaVal "), fname));
                args_v = hexa_array_push(args_v, fname);
            }
            HexaVal sig = hexa_str_join(params_v, hexa_str(""));
            HexaVal call_args = hexa_str_join(args_v, hexa_str(""));
            return
                hexa_add(hexa_str("HexaVal Val("),
                hexa_add(sig,
                hexa_add(hexa_str(") {\n    return hexa_valstruct_new_v("),
                hexa_add(call_args,
                hexa_str(");\n}\n")))));
        }
        // else fall through — mismatched field count; legacy emit covers it.
    }
    HexaVal params = hexa_array_new();
    HexaVal keys_parts = hexa_array_new();
    HexaVal vals_parts = hexa_array_new();
    HexaVal nfields = hexa_int(hexa_len(hexa_map_get(node, "fields")));
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (nfields).i))) {
        HexaVal f = hexa_index_get(hexa_map_get(node, "fields"), i);
        HexaVal fname = hexa_map_get(f, "name");
        params = hexa_array_push(params, hexa_add(hexa_str("HexaVal "), fname));
        if (hexa_truthy(hexa_bool((i).i > (hexa_int(0)).i))) {
            keys_parts = hexa_array_push(keys_parts, hexa_str(", "));
            vals_parts = hexa_array_push(vals_parts, hexa_str(", "));
        }
        keys_parts = hexa_array_push(keys_parts, hexa_add(hexa_add(hexa_str("\""), fname), hexa_str("\"")));
        vals_parts = hexa_array_push(vals_parts, fname);
        i = hexa_add(i, hexa_int(1));
    }
    HexaVal p = hexa_str_join(params, hexa_str(", "));
    if (hexa_truthy(hexa_eq(p, hexa_str("")))) {
        p = hexa_str("void");
    }
    HexaVal body = hexa_str("");
    // Handle zero-field structs: still need valid array syntax.
    if (hexa_truthy(hexa_eq(nfields, hexa_int(0)))) {
        body = hexa_add(
            hexa_add(hexa_str("    return hexa_struct_pack_map(\""), name),
            hexa_str("\", 0, (const char* const*)0, (const HexaVal*)0);\n"));
    } else {
        HexaVal nstr = hexa_to_string(nfields);
        body = hexa_add(
            hexa_add(hexa_add(hexa_str("    static const char* const _k[] = {"), hexa_str_join(keys_parts, hexa_str(""))),
                     hexa_str("};\n")),
            hexa_add(hexa_add(hexa_str("    HexaVal _v[] = {"), hexa_str_join(vals_parts, hexa_str(""))),
                     hexa_str("};\n")));
        body = hexa_add(body,
            hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("    return hexa_struct_pack_map(\""), name),
                                       hexa_str("\", ")), nstr),
                     hexa_str(", _k, _v);\n")));
    }
    return hexa_add(
        hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("HexaVal "), name), hexa_str("(")), p), hexa_str(") {\n")),
        hexa_add(body, hexa_str("}\n")));
    return hexa_void();
}


HexaVal gen2_enum_decl(HexaVal node) {
    HexaVal name = hexa_map_get(node, "name");
    HexaVal parts = hexa_array_new();
    parts = hexa_array_push(parts, hexa_add(hexa_add(hexa_str("// enum "), name), hexa_str("\n")));
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(hexa_map_get(node, "variants")))).i))) {
        HexaVal v = hexa_index_get(hexa_map_get(node, "variants"), i);
        parts = hexa_array_push(parts, hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("#define "), name), hexa_str("_")), hexa_map_get(v, "name")), hexa_str(" hexa_int(")), hexa_to_string(i)), hexa_str(")\n")));
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_str_join(parts, hexa_str(""));
    return hexa_void();
}


HexaVal gen2_match_stmt(HexaVal node, HexaVal depth) {
    HexaVal pad = gen2_indent(depth);
    HexaVal scrutinee = gen2_expr(hexa_map_get(node, "left"));
    HexaVal chunks = hexa_array_new();
    chunks = hexa_array_push(chunks, hexa_add(pad, hexa_str("{\n")));
    HexaVal pad2 = gen2_indent(hexa_add(depth, hexa_int(1)));
    chunks = hexa_array_push(chunks, hexa_add(hexa_add(hexa_add(pad2, hexa_str("HexaVal __match_val = ")), scrutinee), hexa_str(";\n")));
    HexaVal ai = hexa_int(0);
    while (hexa_truthy(hexa_bool((ai).i < (hexa_int(hexa_len(hexa_map_get(node, "arms")))).i))) {
        HexaVal arm = hexa_index_get(hexa_map_get(node, "arms"), ai);
        HexaVal pat = hexa_map_get(arm, "left");
        HexaVal is_wildcard = hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(pat), hexa_str("string"))))) && hexa_truthy(hexa_eq(hexa_map_get(pat, "kind"), hexa_str("Wildcard"))));
        if (hexa_truthy(is_wildcard)) {
            if (hexa_truthy(hexa_bool((ai).i > (hexa_int(0)).i))) {
                chunks = hexa_array_push(chunks, hexa_str(" else {\n"));
            } else {
                chunks = hexa_array_push(chunks, hexa_add(pad2, hexa_str("{\n")));
            }
        } else {
            HexaVal cond = gen2_match_cond(pat, hexa_str("__match_val"));
            if (hexa_truthy(hexa_eq(ai, hexa_int(0)))) {
                chunks = hexa_array_push(chunks, hexa_add(hexa_add(hexa_add(pad2, hexa_str("if (")), cond), hexa_str(") {\n")));
            } else {
                chunks = hexa_array_push(chunks, hexa_add(hexa_add(hexa_str(" else if ("), cond), hexa_str(") {\n")));
            }
        }
        HexaVal bi = hexa_int(0);
        while (hexa_truthy(hexa_bool((bi).i < (hexa_int(hexa_len(hexa_map_get(arm, "body")))).i))) {
            chunks = hexa_array_push(chunks, gen2_stmt(hexa_index_get(hexa_map_get(arm, "body"), bi), hexa_add(depth, hexa_int(2))));
            bi = hexa_add(bi, hexa_int(1));
        }
        chunks = hexa_array_push(chunks, hexa_add(pad2, hexa_str("}")));
        ai = hexa_add(ai, hexa_int(1));
    }
    chunks = hexa_array_push(chunks, hexa_add(hexa_add(hexa_str("\n"), pad), hexa_str("}\n")));
    return hexa_str_join(chunks, hexa_str(""));
    return hexa_void();
}


HexaVal gen2_match_cond(HexaVal pat, HexaVal scrutinee_var) {
    if (hexa_truthy(hexa_eq(hexa_type_of(pat), hexa_str("string")))) {
        return hexa_str("1");
    }
    HexaVal k = hexa_map_get(pat, "kind");
    if (hexa_truthy(hexa_eq(k, hexa_str("Wildcard")))) {
        return hexa_str("1");
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("IntLit")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_truthy(hexa_eq("), scrutinee_var), hexa_str(", hexa_int(")), hexa_map_get(pat, "value")), hexa_str(")))"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("FloatLit")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_truthy(hexa_eq("), scrutinee_var), hexa_str(", hexa_float(")), hexa_map_get(pat, "value")), hexa_str(")))"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("StringLit")))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_truthy(hexa_eq("), scrutinee_var), hexa_str(", hexa_str(\"")), c_escape(hexa_map_get(pat, "value"))), hexa_str("\")))"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("BoolLit")))) {
        if (hexa_truthy(hexa_eq(hexa_map_get(pat, "value"), hexa_str("true")))) {
            return hexa_add(hexa_add(hexa_str("hexa_truthy(hexa_eq("), scrutinee_var), hexa_str(", hexa_bool(1)))"));
        }
        return hexa_add(hexa_add(hexa_str("hexa_truthy(hexa_eq("), scrutinee_var), hexa_str(", hexa_bool(0)))"));
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("EnumPath")))) {
        HexaVal enum_const = hexa_add(hexa_add(hexa_map_get(pat, "name"), hexa_str("_")), hexa_map_get(pat, "value"));
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_truthy(hexa_eq("), scrutinee_var), hexa_str(", ")), enum_const), hexa_str("))"));
    }
    return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_truthy(hexa_eq("), scrutinee_var), hexa_str(", ")), gen2_expr(pat)), hexa_str("))"));
    return hexa_void();
}


HexaVal gen2_match_expr(HexaVal node) {
    HexaVal scrutinee = gen2_expr(hexa_map_get(node, "left"));
    HexaVal arms = hexa_map_get(node, "arms");
    return gen2_match_ternary(arms, scrutinee, hexa_int(0));
    return hexa_void();
}


HexaVal gen2_match_ternary(HexaVal arms, HexaVal scrutinee_c, HexaVal idx) {
    if (hexa_truthy(hexa_bool((idx).i >= (hexa_int(hexa_len(arms))).i))) {
        return hexa_str("hexa_void()");
    }
    HexaVal arm = hexa_index_get(arms, idx);
    HexaVal pat = hexa_map_get(arm, "left");
    HexaVal is_wildcard = hexa_bool(hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(pat), hexa_str("string"))))) && hexa_truthy(hexa_eq(hexa_map_get(pat, "kind"), hexa_str("Wildcard"))));
    HexaVal val = gen2_arm_value(hexa_map_get(arm, "body"));
    if (hexa_truthy(hexa_bool(hexa_truthy(is_wildcard) || hexa_truthy(hexa_eq(idx, hexa_int((hexa_int(hexa_len(arms))).i - (hexa_int(1)).i)))))) {
        if (hexa_truthy(is_wildcard)) {
            return val;
        }
        HexaVal cond = gen2_match_cond(pat, scrutinee_c);
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("("), cond), hexa_str(" ? ")), val), hexa_str(" : hexa_void())"));
    }
    HexaVal cond = gen2_match_cond(pat, scrutinee_c);
    HexaVal rest = gen2_match_ternary(arms, scrutinee_c, hexa_add(idx, hexa_int(1)));
    return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("("), cond), hexa_str(" ? ")), val), hexa_str(" : ")), rest), hexa_str(")"));
    return hexa_void();
}


HexaVal gen2_arm_value(HexaVal body) {
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(hexa_type_of(body), hexa_str("string"))) || hexa_truthy(hexa_eq(hexa_int(hexa_len(body)), hexa_int(0)))))) {
        return hexa_str("hexa_void()");
    }
    HexaVal last = hexa_index_get(body, hexa_int((hexa_int(hexa_len(body))).i - (hexa_int(1)).i));
    if (hexa_truthy(hexa_eq(hexa_map_get(last, "kind"), hexa_str("ExprStmt")))) {
        return gen2_expr(hexa_map_get(last, "left"));
    }
    if (hexa_truthy(hexa_eq(hexa_map_get(last, "kind"), hexa_str("ReturnStmt")))) {
        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(last, "left")), hexa_str("string")))))) {
            return gen2_expr(hexa_map_get(last, "left"));
        }
    }
    return hexa_str("hexa_void()");
    return hexa_void();
}


HexaVal _method_registry_lookup(HexaVal name) {
    HexaVal out = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(_method_registry))).i))) {
        HexaVal e = hexa_index_get(_method_registry, i);
        if (hexa_truthy(hexa_eq(hexa_index_get(e, hexa_int(0)), name))) {
            out = hexa_array_push(out, hexa_index_get(e, hexa_int(1)));
        }
        i = hexa_add(i, hexa_int(1));
    }
    return out;
    return hexa_void();
}


HexaVal gen2_impl_block(HexaVal node) {
    HexaVal type_n = hexa_map_get(node, "target");
    HexaVal fwd_chunks = hexa_array_new();
    HexaVal fn_chunks = hexa_array_new();
    HexaVal methods = hexa_map_get(node, "methods");
    HexaVal mi = hexa_int(0);
    while (hexa_truthy(hexa_bool((mi).i < (hexa_int(hexa_len(methods))).i))) {
        HexaVal m = hexa_index_get(methods, mi);
        HexaVal mangled = hexa_add(hexa_add(type_n, hexa_str("__")), hexa_map_get(m, "name"));
        _method_registry = hexa_array_push(_method_registry, hexa_array_push(hexa_array_push(hexa_array_new(), hexa_map_get(m, "name")), type_n));
        HexaVal params = hexa_array_new();
        HexaVal pi = hexa_int(0);
        while (hexa_truthy(hexa_bool((pi).i < (hexa_int(hexa_len(hexa_map_get(m, "params")))).i))) {
            params = hexa_array_push(params, hexa_add(hexa_str("HexaVal "), hexa_map_get(hexa_index_get(hexa_map_get(m, "params"), pi), "name")));
            pi = hexa_add(pi, hexa_int(1));
        }
        HexaVal p = hexa_str_join(params, hexa_str(", "));
        if (hexa_truthy(hexa_eq(p, hexa_str("")))) {
            p = hexa_str("void");
        }
        fwd_chunks = hexa_array_push(fwd_chunks, hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("HexaVal "), mangled), hexa_str("(")), p), hexa_str(");\n")));
        HexaVal body_chunks = hexa_array_new();
        body_chunks = hexa_array_push(body_chunks, hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("HexaVal "), mangled), hexa_str("(")), p), hexa_str(") {\n")));
        HexaVal bi = hexa_int(0);
        while (hexa_truthy(hexa_bool((bi).i < (hexa_int(hexa_len(hexa_map_get(m, "body")))).i))) {
            body_chunks = hexa_array_push(body_chunks, gen2_stmt(hexa_index_get(hexa_map_get(m, "body"), bi), hexa_int(1)));
            bi = hexa_add(bi, hexa_int(1));
        }
        body_chunks = hexa_array_push(body_chunks, hexa_str("    return hexa_void();\n}\n\n"));
        fn_chunks = hexa_array_push(fn_chunks, hexa_str_join(body_chunks, hexa_str("")));
        mi = hexa_add(mi, hexa_int(1));
    }
    return hexa_map_set(hexa_map_set(hexa_map_new(), "forward", hexa_str_join(fwd_chunks, hexa_str(""))), "fn_code", hexa_str_join(fn_chunks, hexa_str("")));
    return hexa_void();
}


HexaVal _is_known_fn_global(HexaVal name) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(_known_fn_globals))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(_known_fn_globals, i), name))) {
            return hexa_bool(1);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_bool(0);
    return hexa_void();
}


HexaVal _is_known_nonlocal(HexaVal name) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(_known_nonlocal_names))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(_known_nonlocal_names, i), name))) {
            return hexa_bool(1);
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_bool(0);
    return hexa_void();
}


HexaVal _is_known_global(HexaVal name) {
    return _is_known_nonlocal(name);
    return hexa_void();
}


HexaVal _is_builtin_name(HexaVal name) {
    if (hexa_truthy(hexa_eq(name, hexa_str("println")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("print")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("eprintln")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("len")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("to_string")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("type_of")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("sqrt")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("floor")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("ceil")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("abs")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("round")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("ln")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("log")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("log10")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("exp")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("sin")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("cos")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("pow")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("to_float")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("to_int")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("min")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("max")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("format")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("args")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("exec")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("read_file")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("write_file")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("file_exists")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("char_code")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("is_alpha")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("is_digit")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("is_alphanumeric")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("true")))) {
        return hexa_bool(1);
    }
    if (hexa_truthy(hexa_eq(name, hexa_str("false")))) {
        return hexa_bool(1);
    }
    return hexa_bool(0);
    return hexa_void();
}


HexaVal _add_unique(HexaVal list, HexaVal name) {
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(list))).i))) {
        if (hexa_truthy(hexa_eq(hexa_index_get(list, i), name))) {
            return list;
        }
        i = hexa_add(i, hexa_int(1));
    }
    return hexa_array_push(list, name);
    return hexa_void();
}


HexaVal gen2_collect_free(HexaVal node, HexaVal bound, HexaVal out) {
    if (hexa_truthy(hexa_eq(hexa_type_of(node), hexa_str("string")))) {
        return out;
    }
    if (hexa_truthy(hexa_eq(hexa_type_of(node), hexa_str("array")))) {
        HexaVal local = bound;
        HexaVal result = out;
        HexaVal i = hexa_int(0);
        while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(node))).i))) {
            HexaVal s = hexa_index_get(node, i);
            if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(s), hexa_str("string")))))) {
                HexaVal sk = hexa_map_get(s, "kind");
                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(sk, hexa_str("LetStmt"))) || hexa_truthy(hexa_eq(sk, hexa_str("LetMutStmt")))))) {
                    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(s, "left")), hexa_str("string")))))) {
                        result = gen2_collect_free(hexa_map_get(s, "left"), local, result);
                    }
                    local = hexa_array_push(local, hexa_map_get(s, "name"));
                } else {
                    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(sk, hexa_str("ForStmt"))) || hexa_truthy(hexa_eq(sk, hexa_str("ForInStmt")))))) {
                        if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(s, "left")), hexa_str("string")))))) {
                            result = gen2_collect_free(hexa_map_get(s, "left"), local, result);
                        }
                        HexaVal body_local = hexa_array_push(local, hexa_map_get(s, "name"));
                        if (hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(s, "body")), hexa_str("array")))) {
                            result = gen2_collect_free(hexa_map_get(s, "body"), body_local, result);
                        }
                    } else {
                        result = gen2_collect_free(s, local, result);
                    }
                }
            }
            i = hexa_add(i, hexa_int(1));
        }
        return result;
    }
    HexaVal k = hexa_map_get(node, "kind");
    if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("IntLit"))) || hexa_truthy(hexa_eq(k, hexa_str("FloatLit"))))) || hexa_truthy(hexa_eq(k, hexa_str("StringLit"))))) || hexa_truthy(hexa_eq(k, hexa_str("BoolLit"))))) || hexa_truthy(hexa_eq(k, hexa_str("CharLit"))))) || hexa_truthy(hexa_eq(k, hexa_str("Wildcard")))))) {
        return out;
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Ident")))) {
        HexaVal name = hexa_map_get(node, "name");
        if (hexa_truthy(_is_known_global(name))) {
            return out;
        }
        if (hexa_truthy(_is_builtin_name(name))) {
            return out;
        }
        HexaVal bi = hexa_int(0);
        while (hexa_truthy(hexa_bool((bi).i < (hexa_int(hexa_len(bound))).i))) {
            if (hexa_truthy(hexa_eq(hexa_index_get(bound, bi), name))) {
                return out;
            }
            bi = hexa_add(bi, hexa_int(1));
        }
        return _add_unique(out, name);
    }
    if (hexa_truthy(hexa_eq(k, hexa_str("Lambda")))) {
        HexaVal inner_bound = bound;
        HexaVal pi = hexa_int(0);
        while (hexa_truthy(hexa_bool((pi).i < (hexa_int(hexa_len(hexa_map_get(node, "params")))).i))) {
            inner_bound = hexa_array_push(inner_bound, hexa_index_get(hexa_map_get(node, "params"), pi));
            pi = hexa_add(pi, hexa_int(1));
        }
        return gen2_collect_free(hexa_map_get(node, "left"), inner_bound, out);
    }
    HexaVal result = out;
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "left")), hexa_str("string")))))) {
        result = gen2_collect_free(hexa_map_get(node, "left"), bound, result);
    }
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "right")), hexa_str("string")))))) {
        result = gen2_collect_free(hexa_map_get(node, "right"), bound, result);
    }
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "cond")), hexa_str("string")))))) {
        result = gen2_collect_free(hexa_map_get(node, "cond"), bound, result);
    }
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "then_body")), hexa_str("string")))))) {
        result = gen2_collect_free(hexa_map_get(node, "then_body"), bound, result);
    }
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "else_body")), hexa_str("string")))))) {
        result = gen2_collect_free(hexa_map_get(node, "else_body"), bound, result);
    }
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "body")), hexa_str("string")))))) {
        result = gen2_collect_free(hexa_map_get(node, "body"), bound, result);
    }
    if (hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "args")), hexa_str("array")))) {
        HexaVal ai = hexa_int(0);
        while (hexa_truthy(hexa_bool((ai).i < (hexa_int(hexa_len(hexa_map_get(node, "args")))).i))) {
            result = gen2_collect_free(hexa_index_get(hexa_map_get(node, "args"), ai), bound, result);
            ai = hexa_add(ai, hexa_int(1));
        }
    }
    if (hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "items")), hexa_str("array")))) {
        HexaVal ii = hexa_int(0);
        while (hexa_truthy(hexa_bool((ii).i < (hexa_int(hexa_len(hexa_map_get(node, "items")))).i))) {
            result = gen2_collect_free(hexa_index_get(hexa_map_get(node, "items"), ii), bound, result);
            ii = hexa_add(ii, hexa_int(1));
        }
    }
    if (hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(node, "arms")), hexa_str("array")))) {
        HexaVal mi = hexa_int(0);
        while (hexa_truthy(hexa_bool((mi).i < (hexa_int(hexa_len(hexa_map_get(node, "arms")))).i))) {
            HexaVal arm = hexa_index_get(hexa_map_get(node, "arms"), mi);
            if (hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(arm, "body")), hexa_str("array")))) {
                result = gen2_collect_free(hexa_map_get(arm, "body"), bound, result);
            }
            mi = hexa_add(mi, hexa_int(1));
        }
    }
    return result;
    return hexa_void();
}


HexaVal gen2_lambda_expr(HexaVal node) {
    HexaVal id = _lambda_counter;
    _lambda_counter = hexa_add(_lambda_counter, hexa_int(1));
    HexaVal fn_name = hexa_add(hexa_str("__hexa_lambda_"), hexa_to_string(id));
    HexaVal bound = hexa_array_new();
    HexaVal pi = hexa_int(0);
    while (hexa_truthy(hexa_bool((pi).i < (hexa_int(hexa_len(hexa_map_get(node, "params")))).i))) {
        bound = hexa_array_push(bound, hexa_index_get(hexa_map_get(node, "params"), pi));
        pi = hexa_add(pi, hexa_int(1));
    }
    HexaVal free_vars = gen2_collect_free(hexa_map_get(node, "left"), bound, hexa_array_new());
    HexaVal arity = hexa_int(hexa_len(hexa_map_get(node, "params")));
    HexaVal params = hexa_array_new();
    params = hexa_array_push(params, hexa_str("HexaVal __env"));
    pi = hexa_int(0);
    while (hexa_truthy(hexa_bool((pi).i < (hexa_int(hexa_len(hexa_map_get(node, "params")))).i))) {
        params = hexa_array_push(params, hexa_add(hexa_str("HexaVal "), hexa_index_get(hexa_map_get(node, "params"), pi)));
        pi = hexa_add(pi, hexa_int(1));
    }
    HexaVal p = hexa_str_join(params, hexa_str(", "));
    if (hexa_truthy(hexa_eq(p, hexa_str("")))) {
        p = hexa_str("void");
    }
    HexaVal body_parts = hexa_array_new();
    if (hexa_truthy(hexa_bool((hexa_int(hexa_len(free_vars))).i > (hexa_int(0)).i))) {
        HexaVal fi = hexa_int(0);
        while (hexa_truthy(hexa_bool((fi).i < (hexa_int(hexa_len(free_vars))).i))) {
            body_parts = hexa_array_push(body_parts, hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("    HexaVal "), hexa_index_get(free_vars, fi)), hexa_str(" = hexa_array_get(__env, ")), hexa_to_string(fi)), hexa_str(");\n")));
            fi = hexa_add(fi, hexa_int(1));
        }
    }
    HexaVal lbody = hexa_map_get(node, "left");
    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(lbody), hexa_str("string")))))) {
        if (hexa_truthy(hexa_eq(hexa_type_of(lbody), hexa_str("array")))) {
            HexaVal bi = hexa_int(0);
            while (hexa_truthy(hexa_bool((bi).i < (hexa_int(hexa_len(lbody))).i))) {
                body_parts = hexa_array_push(body_parts, gen2_stmt(hexa_index_get(lbody, bi), hexa_int(1)));
                bi = hexa_add(bi, hexa_int(1));
            }
        } else {
            body_parts = hexa_array_push(body_parts, hexa_add(hexa_add(hexa_str("    return "), gen2_expr(lbody)), hexa_str(";\n")));
        }
    }
    HexaVal fn_def = hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("HexaVal "), fn_name), hexa_str("(")), p), hexa_str(") {\n")), hexa_str_join(body_parts, hexa_str(""))), hexa_str("    return hexa_void();\n}\n\n"));
    _lambda_def_parts = hexa_array_push(_lambda_def_parts, fn_def);
    if (hexa_truthy(hexa_eq(hexa_int(hexa_len(free_vars)), hexa_int(0)))) {
        return hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_closure_new((void*)&"), fn_name), hexa_str(", ")), hexa_to_string(arity)), hexa_str(", hexa_array_new())"));
    }
    HexaVal env_expr = hexa_str("hexa_array_new()");
    HexaVal fi = hexa_int(0);
    while (hexa_truthy(hexa_bool((fi).i < (hexa_int(hexa_len(free_vars))).i))) {
        env_expr = hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_array_push("), env_expr), hexa_str(", ")), hexa_index_get(free_vars, fi)), hexa_str(")"));
        fi = hexa_add(fi, hexa_int(1));
    }
    return hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("hexa_closure_new((void*)&"), fn_name), hexa_str(", ")), hexa_to_string(arity)), hexa_str(", ")), env_expr), hexa_str(")"));
    return hexa_void();
}


HexaVal codegen_c2_full(HexaVal ast) {
    _lambda_counter = hexa_int(0);
    _lambda_def_parts = hexa_array_new();
    _method_registry = hexa_array_new();
    _known_fn_globals = hexa_array_new();
    _known_nonlocal_names = hexa_array_new();
    _gen2_declared_names = hexa_array_new();
    /* rt#37: reset inline-cache slot accumulator per codegen invocation. */
    _ic_def_parts = hexa_array_new();
    _ic_def_parts_init = 1;
    HexaVal gi = hexa_int(0);
    while (hexa_truthy(hexa_bool((gi).i < (hexa_int(hexa_len(ast))).i))) {
        HexaVal gk = hexa_map_get(hexa_index_get(ast, gi), "kind");
        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(gk, hexa_str("FnDecl"))) || hexa_truthy(hexa_eq(gk, hexa_str("ExternFnDecl"))))) || hexa_truthy(hexa_eq(gk, hexa_str("PureFnDecl"))))) || hexa_truthy(hexa_eq(gk, hexa_str("OptimizeFnStmt")))))) {
            _known_fn_globals = hexa_array_push(_known_fn_globals, hexa_map_get(hexa_index_get(ast, gi), "name"));
            _known_nonlocal_names = hexa_array_push(_known_nonlocal_names, hexa_map_get(hexa_index_get(ast, gi), "name"));
        } else {
            if (hexa_truthy(hexa_eq(gk, hexa_str("StructDecl")))) {
                _known_fn_globals = hexa_array_push(_known_fn_globals, hexa_map_get(hexa_index_get(ast, gi), "name"));
                _known_nonlocal_names = hexa_array_push(_known_nonlocal_names, hexa_map_get(hexa_index_get(ast, gi), "name"));
            } else {
                if (hexa_truthy(hexa_eq(gk, hexa_str("EnumDecl")))) {
                    _known_nonlocal_names = hexa_array_push(_known_nonlocal_names, hexa_map_get(hexa_index_get(ast, gi), "name"));
                } else {
                    if (hexa_truthy(hexa_eq(gk, hexa_str("ComptimeConst")))) {
                        _known_nonlocal_names = hexa_array_push(_known_nonlocal_names, hexa_map_get(hexa_index_get(ast, gi), "name"));
                    } else {
                        if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(gk, hexa_str("LetMutStmt"))) || hexa_truthy(hexa_eq(gk, hexa_str("LetStmt")))))) {
                            _known_nonlocal_names = hexa_array_push(_known_nonlocal_names, hexa_map_get(hexa_index_get(ast, gi), "name"));
                        }
                    }
                }
            }
        }
        gi = hexa_add(gi, hexa_int(1));
    }
    HexaVal fwd_parts = hexa_array_new();
    HexaVal global_parts = hexa_array_new();
    HexaVal fn_parts = hexa_array_new();
    HexaVal main_parts = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (hexa_truthy(hexa_bool((i).i < (hexa_int(hexa_len(ast))).i))) {
        HexaVal k = hexa_map_get(hexa_index_get(ast, i), "kind");
        if (hexa_truthy(hexa_eq(k, hexa_str("FnDecl")))) {
            fwd_parts = hexa_array_push(fwd_parts, hexa_add(gen2_fn_forward(hexa_index_get(ast, i)), hexa_str("\n")));
            fn_parts = hexa_array_push(fn_parts, hexa_add(gen2_fn_decl(hexa_index_get(ast, i)), hexa_str("\n\n")));
        } else {
            if (hexa_truthy(hexa_eq(k, hexa_str("ExternFnDecl")))) {
                HexaVal ew = gen2_extern_wrapper(hexa_index_get(ast, i));
                global_parts = hexa_array_push(global_parts, hexa_map_get(ew, "global"));
                fwd_parts = hexa_array_push(fwd_parts, hexa_add(hexa_map_get(ew, "forward"), hexa_str("\n")));
                fn_parts = hexa_array_push(fn_parts, hexa_add(hexa_map_get(ew, "fn_code"), hexa_str("\n\n")));
                main_parts = hexa_array_push(main_parts, hexa_map_get(ew, "init"));
            } else {
                if (hexa_truthy(hexa_eq(k, hexa_str("StructDecl")))) {
                    fn_parts = hexa_array_push(fn_parts, hexa_add(gen2_struct_decl(hexa_index_get(ast, i)), hexa_str("\n\n")));
                } else {
                    if (hexa_truthy(hexa_eq(k, hexa_str("EnumDecl")))) {
                        fn_parts = hexa_array_push(fn_parts, hexa_add(gen2_enum_decl(hexa_index_get(ast, i)), hexa_str("\n")));
                    } else {
                        if (hexa_truthy(hexa_eq(k, hexa_str("ImplBlock")))) {
                            HexaVal ib = gen2_impl_block(hexa_index_get(ast, i));
                            fwd_parts = hexa_array_push(fwd_parts, hexa_map_get(ib, "forward"));
                            fn_parts = hexa_array_push(fn_parts, hexa_map_get(ib, "fn_code"));
                        } else {
                            if (hexa_truthy(hexa_eq(k, hexa_str("TraitDecl")))) {
                            } else {
                                if (hexa_truthy(hexa_bool(hexa_truthy(hexa_eq(k, hexa_str("LetMutStmt"))) || hexa_truthy(hexa_eq(k, hexa_str("LetStmt")))))) {
                                    /* bt-53: mangle reserved runtime names (TAG_INT etc.) */
                                    HexaVal __user_name = hexa_mangle_ident(hexa_map_get(hexa_index_get(ast, i), "name"));
                                    global_parts = hexa_array_push(global_parts, hexa_add(hexa_add(hexa_str("HexaVal "), __user_name), hexa_str(";\n")));
                                    if (hexa_truthy(hexa_bool(!hexa_truthy(hexa_eq(hexa_type_of(hexa_map_get(hexa_index_get(ast, i), "left")), hexa_str("string")))))) {
                                        main_parts = hexa_array_push(main_parts, hexa_add(hexa_add(hexa_add(hexa_add(hexa_str("    "), __user_name), hexa_str(" = ")), gen2_expr(hexa_map_get(hexa_index_get(ast, i), "left"))), hexa_str(";\n")));
                                    } else {
                                        main_parts = hexa_array_push(main_parts, hexa_add(hexa_add(hexa_str("    "), __user_name), hexa_str(" = hexa_void();\n")));
                                    }
                                } else {
                                    main_parts = hexa_array_push(main_parts, gen2_stmt(hexa_index_get(ast, i), hexa_int(1)));
                                }
                            }
                        }
                    }
                }
            }
        }
        i = hexa_add(i, hexa_int(1));
    }
    HexaVal out_parts = hexa_array_new();
    out_parts = hexa_array_push(out_parts, hexa_str("// Generated by HEXA self-host compiler\n"));
    out_parts = hexa_array_push(out_parts, hexa_str("#include \"runtime.c\"\n#include <unistd.h>\n\n"));
    out_parts = hexa_array_push(out_parts, hexa_str_join(fwd_parts, hexa_str("")));
    out_parts = hexa_array_push(out_parts, hexa_str("\n"));
    out_parts = hexa_array_push(out_parts, hexa_str_join(global_parts, hexa_str("")));
    out_parts = hexa_array_push(out_parts, hexa_str("\n"));
    /* rt#37: emit inline-cache slot defs before fn bodies reference them. */
    if (_ic_def_parts_init) {
        out_parts = hexa_array_push(out_parts, hexa_str_join(_ic_def_parts, hexa_str("")));
        out_parts = hexa_array_push(out_parts, hexa_str("\n"));
    }
    out_parts = hexa_array_push(out_parts, hexa_str_join(_lambda_def_parts, hexa_str("")));
    out_parts = hexa_array_push(out_parts, hexa_str_join(fn_parts, hexa_str("")));
    out_parts = hexa_array_push(out_parts, hexa_str("int main(int argc, char** argv) {\n    hexa_set_args(argc, argv);\n"));
    out_parts = hexa_array_push(out_parts, hexa_str_join(main_parts, hexa_str("")));
    out_parts = hexa_array_push(out_parts, hexa_str("    return 0;\n}\n"));
    return hexa_str_join(out_parts, hexa_str(""));
    return hexa_void();
}



int main(int argc, char** argv) {
    if (argc < 3) {
        printf("hexa-cc: HEXA self-hosted compiler\nUsage: hexa-cc <input.hexa> <output.c>\n");
        return 1;
    }
    HexaVal src = hexa_read_file(hexa_str(argv[1]));
    HexaVal tokens = tokenize(src);
    HexaVal ast = parse(tokens);
    long long __err_n = (p_error_count()).i;
    if (__err_n > 0) {
        fprintf(stderr, "error: %s — %lld parse error(s)\n", argv[1], __err_n);
        return 2;
    }
    HexaVal c_code = codegen_c2_full(ast);
    hexa_write_file(hexa_str(argv[2]), c_code);
    printf("OK: %s\n", argv[2]);
    return 0;
}
