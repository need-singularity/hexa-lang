#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char hexa_arena[65536];
static size_t hexa_arena_p = 0;
static char* hexa_alloc(size_t n) {
    if (hexa_arena_p + n >= sizeof(hexa_arena)) { fprintf(stderr, "hexa_arena overflow\n"); exit(1); }
    char* p = &hexa_arena[hexa_arena_p];
    hexa_arena_p += n;
    return p;
}
static const char* hexa_concat(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    char* p = hexa_alloc(la + lb + 1);
    memcpy(p, a, la); memcpy(p + la, b, lb); p[la + lb] = 0;
    return p;
}
static const char* hexa_int_to_str(long v) {
    char* p = hexa_alloc(24);
    snprintf(p, 24, "%ld", v);
    return p;
}
static const char* hexa_substr(const char* s, long a, long b) {
    long sl = (long)strlen(s);
    if (a < 0) a = 0; if (b > sl) b = sl; if (a > b) a = b;
    long n = b - a;
    char* p = hexa_alloc(n + 1);
    memcpy(p, s + a, n); p[n] = 0;
    return p;
}
static long hexa_str_len(const char* s) { return (long)strlen(s); }
static const char* hexa_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "hexa_read_file: %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* p = hexa_alloc(sz + 1);
    fread(p, 1, sz, f); p[sz] = 0; fclose(f);
    return p;
}
static long hexa_write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = strlen(content);
    fwrite(content, 1, n, f); fclose(f);
    return (long)n;
}
typedef struct { long* d; long n; long cap; } hexa_arr;
static hexa_arr hexa_arr_new(void) { hexa_arr a = {NULL, 0, 0}; return a; }
static hexa_arr hexa_arr_lit(const long* items, long n) {
    hexa_arr a; a.d = (long*)malloc((n>0?n:1)*sizeof(long)); a.n = n; a.cap = (n>0?n:1);
    if (n > 0) memcpy(a.d, items, n*sizeof(long));
    return a;
}
static hexa_arr hexa_arr_push(hexa_arr a, long x) {
    if (a.n >= a.cap) {
        a.cap = a.cap ? a.cap * 2 : 4;
        a.d = (long*)realloc(a.d, a.cap * sizeof(long));
    }
    a.d[a.n++] = x;
    return a;
}
static long hexa_arr_len(hexa_arr a) { return a.n; }
static long hexa_arr_get(hexa_arr a, long i) { return a.d[i]; }
static hexa_arr hexa_chars(const char* s) {
    long n = (long)strlen(s);
    hexa_arr a; a.d = (long*)malloc((n>0?n:1)*sizeof(long)); a.n = n; a.cap = (n>0?n:1);
    for (long i = 0; i < n; i++) a.d[i] = (long)(unsigned char)s[i];
    return a;
}
#include <ctype.h>
static long hexa_is_alpha(long c) { return (long)(isalpha((int)c) ? 1 : 0); }
static long hexa_is_alnum(long c) { return (long)(isalnum((int)c) ? 1 : 0); }
static int hexa_main_argc = 0;
static char** hexa_main_argv = NULL;
static hexa_arr hexa_args(void) {
    hexa_arr a; a.n = hexa_main_argc; a.cap = hexa_main_argc;
    a.d = (long*)malloc((hexa_main_argc>0?hexa_main_argc:1)*sizeof(long));
    for (int i = 0; i < hexa_main_argc; i++) a.d[i] = (long)hexa_main_argv[i];
    return a;
}

long cs_at(long cs, long i);
long is_digit_s(long c);
long is_alpha_at(long cs, long i);
long is_alnum_at(long cs, long i);
long is_space_s(long c);
long slice_str(long cs, long a, long b);
long mk(long k, long v);
long tokenize(long src);
long empty();
long n_int(long v);
long n_float(long v);
long n_str(long v);
long n_bool(long v);
long n_ident(long v);
long n_bin(long op, long l, long r);
long n_unary(long op, long e);
long n_call(long callee, long call_args);
long n_let(long name, long val);
long n_assign(long name, long val);
long n_exprstmt(long e);
long n_if(long c, long t, long e);
long n_while(long c, long b);
long n_for_range(long var_name, long lo, long hi, long body);
long n_block(long stmts);
long n_fn(long name, long params, long body);
long n_return(long e);
long n_arrlit(long elems);
long n_index(long arr, long idx);
long n_structdecl(long name, long field_names);
long n_structlit(long name, long vals);
long n_field(long obj, long name);
long n_method(long obj, long name, long m_args);
long p_peek();
long p_kind();
long p_advance();
long p_eat(long k);
long p_skip_nl();
long parse_primary();
long parse_mul();
long parse_add();
long parse_cmp();
long parse_and();
long parse_or();
long parse_expr();
long parse_block();
long parse_if();
long parse_while();
long parse_for();
long parse_struct();
long parse_fn();
long parse_stmt();
long parse_program(long toks);
long struct_register(long name, long fields);
long struct_lookup_fields(long name);
long struct_field_index(long struct_name, long field_name);
long env_lookup(long name);
long env_lookup_len(long name);
long env_set(long name, long ty);
long env_set_arr(long name, long alen);
long env_set_struct(long name, long struct_name);
long type_of(long *node);
long coerce_str(long node, long t);
long emit_expr(long *node);
long emit_println(hexa_arr pl_args);
long pad();
long emit_stmt(long *node);
long find_method_on_param(long *node, long pname, long mname);
long find_param_field(long *node, long pname);
long find_param_indexed(long *node, long pname);
long find_len_call_on_param(long *node, long pname);
long infer_param_c_type(long body, long pname);
long emit_fn_decl(long *node);
long emit_fn_proto(long *node);
long emit_block(long *node);
long emit_runtime();
long emit_program(hexa_arr stmts);
long cli_args();

long cs_at(long cs, long i) {
        return hexa_concat("", hexa_int_to_str((long)(cs[i])));
    }

long is_digit_s(long c) {
        return ((((((((((c == "0") || (c == "1")) || (c == "2")) || (c == "3")) || (c == "4")) || (c == "5")) || (c == "6")) || (c == "7")) || (c == "8")) || (c == "9"));
    }

long is_alpha_at(long cs, long i) {
        if ((cs_at(cs, i) == "_")) {
            return true;
        }
        return hexa_is_alpha(cs[i]);
    }

long is_alnum_at(long cs, long i) {
        if ((cs_at(cs, i) == "_")) {
            return true;
        }
        return hexa_is_alnum(cs[i]);
    }

long is_space_s(long c) {
        return (((c == " ") || (c == "\t")) || (c == "\r"));
    }

long slice_str(long cs, long a, long b) {
        const char* out = "";
        long k = a;
        while ((k < b)) {
            out = hexa_concat(out, hexa_concat("", hexa_int_to_str((long)(cs[k]))));
            k = (k + 1);
        }
        return out;
    }

long mk(long k, long v) {
        return (long[]){k, v};
    }

long tokenize(long src) {
        hexa_arr cs = hexa_chars(src);
        long n = hexa_str_len(cs);
        hexa_arr toks = hexa_arr_new();
        long i = 0;
        while ((i < n)) {
            long c = cs_at(cs, i);
            if (is_space_s(c)) {
                i = (i + 1);
            } else {
                if ((c == "\n")) {
                    toks = hexa_arr_push(toks, mk("NL", "\n"));
                    i = (i + 1);
                } else {
                    if (((((i + 1) < n) && (c == "/")) && (cs_at(cs, (i + 1)) == "/"))) {
                        while (((i < n) && (cs_at(cs, i) != "\n"))) {
                            i = (i + 1);
                        }
                    } else {
                        if (is_digit_s(c)) {
                            long start = i;
                            while (((i < n) && is_digit_s(cs_at(cs, i)))) {
                                i = (i + 1);
                            }
                            long is_float = false;
                            if (((((i < n) && (cs_at(cs, i) == ".")) && ((i + 1) < n)) && is_digit_s(cs_at(cs, (i + 1))))) {
                                is_float = true;
                                i = (i + 1);
                                while (((i < n) && is_digit_s(cs_at(cs, i)))) {
                                    i = (i + 1);
                                }
                            }
                            if (((i < n) && ((cs_at(cs, i) == "e") || (cs_at(cs, i) == "E")))) {
                                is_float = true;
                                i = (i + 1);
                                if (((i < n) && ((cs_at(cs, i) == "+") || (cs_at(cs, i) == "-")))) {
                                    i = (i + 1);
                                }
                                while (((i < n) && is_digit_s(cs_at(cs, i)))) {
                                    i = (i + 1);
                                }
                            }
                            if (is_float) {
                                toks = hexa_arr_push(toks, mk("Float", slice_str(cs, start, i)));
                            } else {
                                toks = hexa_arr_push(toks, mk("Int", slice_str(cs, start, i)));
                            }
                        } else {
                            if (is_alpha_at(cs, i)) {
                                long start = i;
                                while (((i < n) && is_alnum_at(cs, i))) {
                                    i = (i + 1);
                                }
                                long word = slice_str(cs, start, i);
                                if ((word == "let")) {
                                    toks = hexa_arr_push(toks, mk("Let", word));
                                } else {
                                    if ((word == "mut")) {
                                        toks = hexa_arr_push(toks, mk("Mut", word));
                                    } else {
                                        if ((word == "if")) {
                                            toks = hexa_arr_push(toks, mk("If", word));
                                        } else {
                                            if ((word == "else")) {
                                                toks = hexa_arr_push(toks, mk("Else", word));
                                            } else {
                                                if ((word == "while")) {
                                                    toks = hexa_arr_push(toks, mk("While", word));
                                                } else {
                                                    if ((word == "for")) {
                                                        toks = hexa_arr_push(toks, mk("For", word));
                                                    } else {
                                                        if ((word == "in")) {
                                                            toks = hexa_arr_push(toks, mk("In", word));
                                                        } else {
                                                            if ((word == "struct")) {
                                                                toks = hexa_arr_push(toks, mk("Struct", word));
                                                            } else {
                                                                if ((word == "fn")) {
                                                                    toks = hexa_arr_push(toks, mk("Fn", word));
                                                                } else {
                                                                    if ((word == "return")) {
                                                                        toks = hexa_arr_push(toks, mk("Return", word));
                                                                    } else {
                                                                        if ((word == "true")) {
                                                                            toks = hexa_arr_push(toks, mk("True", word));
                                                                        } else {
                                                                            if ((word == "false")) {
                                                                                toks = hexa_arr_push(toks, mk("False", word));
                                                                            } else {
                                                                                toks = hexa_arr_push(toks, mk("Ident", word));
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
                            } else {
                                if ((c == "\")) {
                                    " {
            i = i + 1
            let start = i
            while i < n && cs_at(cs, i) != ";
                                    "";
                                    0;
                                    i = (i + 1);
                                }
                            }
                        }
                    }
                }
            }
            long s = slice_str(cs, start, i);
            if ((i < n)) {
                i = (i + 1);
            }
            toks = hexa_arr_push(toks, mk("Str", s));
        }
        0;
        if ((c == "(")) {
            toks = hexa_arr_push(toks, mk("LP", c));
            i = (i + 1);
        } else {
            if ((c == ")")) {
                toks = hexa_arr_push(toks, mk("RP", c));
                i = (i + 1);
            } else {
                if ((c == ",")) {
                    toks = hexa_arr_push(toks, mk("Comma", c));
                    i = (i + 1);
                } else {
                    if ((c == "=")) {
                        if ((((i + 1) < n) && (cs_at(cs, (i + 1)) == "="))) {
                            toks = hexa_arr_push(toks, mk("EqEq", "=="));
                            i = (i + 2);
                        } else {
                            toks = hexa_arr_push(toks, mk("Eq", c));
                            i = (i + 1);
                        }
                    } else {
                        if ((c == "!")) {
                            if ((((i + 1) < n) && (cs_at(cs, (i + 1)) == "="))) {
                                toks = hexa_arr_push(toks, mk("NotEq", "!="));
                                i = (i + 2);
                            } else {
                                toks = hexa_arr_push(toks, mk("Bang", c));
                                i = (i + 1);
                            }
                        } else {
                            if ((c == "<")) {
                                if ((((i + 1) < n) && (cs_at(cs, (i + 1)) == "="))) {
                                    toks = hexa_arr_push(toks, mk("LE", "<="));
                                    i = (i + 2);
                                } else {
                                    toks = hexa_arr_push(toks, mk("LT", c));
                                    i = (i + 1);
                                }
                            } else {
                                if ((c == ">")) {
                                    if ((((i + 1) < n) && (cs_at(cs, (i + 1)) == "="))) {
                                        toks = hexa_arr_push(toks, mk("GE", ">="));
                                        i = (i + 2);
                                    } else {
                                        toks = hexa_arr_push(toks, mk("GT", c));
                                        i = (i + 1);
                                    }
                                } else {
                                    if ((c == "&")) {
                                        if ((((i + 1) < n) && (cs_at(cs, (i + 1)) == "&"))) {
                                            toks = hexa_arr_push(toks, mk("AndAnd", "&&"));
                                            i = (i + 2);
                                        } else {
                                            i = (i + 1);
                                        }
                                    } else {
                                        if ((c == "|")) {
                                            if ((((i + 1) < n) && (cs_at(cs, (i + 1)) == "|"))) {
                                                toks = hexa_arr_push(toks, mk("OrOr", "||"));
                                                i = (i + 2);
                                            } else {
                                                i = (i + 1);
                                            }
                                        } else {
                                            if ((c == "{")) {
                                                toks = hexa_arr_push(toks, mk("LB", c));
                                                i = (i + 1);
                                            } else {
                                                if ((c == "}")) {
                                                    toks = hexa_arr_push(toks, mk("RB", c));
                                                    i = (i + 1);
                                                } else {
                                                    if ((c == "[")) {
                                                        toks = hexa_arr_push(toks, mk("LSq", c));
                                                        i = (i + 1);
                                                    } else {
                                                        if ((c == "]")) {
                                                            toks = hexa_arr_push(toks, mk("RSq", c));
                                                            i = (i + 1);
                                                        } else {
                                                            if ((c == "%")) {
                                                                toks = hexa_arr_push(toks, mk("Percent", c));
                                                                i = (i + 1);
                                                            } else {
                                                                if ((c == ":")) {
                                                                    toks = hexa_arr_push(toks, mk("Colon", c));
                                                                    i = (i + 1);
                                                                } else {
                                                                    if ((c == ".")) {
                                                                        if ((((i + 1) < n) && (cs_at(cs, (i + 1)) == "."))) {
                                                                            toks = hexa_arr_push(toks, mk("DotDot", ".."));
                                                                            i = (i + 2);
                                                                        } else {
                                                                            toks = hexa_arr_push(toks, mk("Dot", c));
                                                                            i = (i + 1);
                                                                        }
                                                                    } else {
                                                                        if ((c == "+")) {
                                                                            if ((((i + 1) < n) && (cs_at(cs, (i + 1)) == "="))) {
                                                                                toks = hexa_arr_push(toks, mk("PlusEq", "+="));
                                                                                i = (i + 2);
                                                                            } else {
                                                                                toks = hexa_arr_push(toks, mk("Plus", c));
                                                                                i = (i + 1);
                                                                            }
                                                                        } else {
                                                                            if ((c == "-")) {
                                                                                if ((((i + 1) < n) && (cs_at(cs, (i + 1)) == "="))) {
                                                                                    toks = hexa_arr_push(toks, mk("MinusEq", "-="));
                                                                                    i = (i + 2);
                                                                                } else {
                                                                                    toks = hexa_arr_push(toks, mk("Minus", c));
                                                                                    i = (i + 1);
                                                                                }
                                                                            } else {
                                                                                if ((c == "*")) {
                                                                                    if ((((i + 1) < n) && (cs_at(cs, (i + 1)) == "="))) {
                                                                                        toks = hexa_arr_push(toks, mk("StarEq", "*="));
                                                                                        i = (i + 2);
                                                                                    } else {
                                                                                        toks = hexa_arr_push(toks, mk("Star", c));
                                                                                        i = (i + 1);
                                                                                    }
                                                                                } else {
                                                                                    if ((c == "/")) {
                                                                                        if ((((i + 1) < n) && (cs_at(cs, (i + 1)) == "="))) {
                                                                                            toks = hexa_arr_push(toks, mk("SlashEq", "/="));
                                                                                            i = (i + 2);
                                                                                        } else {
                                                                                            toks = hexa_arr_push(toks, mk("Slash", c));
                                                                                            i = (i + 1);
                                                                                        }
                                                                                    } else {
                                                                                        i = (i + 1);
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

long empty() {
        return (long[]){"", "", "", "", "", "", "", "", 0};
    }

long n_int(long v) {
        return (long[]){"Int", v, "", "", "", "", "", "", 0};
    }

long n_float(long v) {
        return (long[]){"Float", v, "", "", "", "", "", "", 0};
    }

long n_str(long v) {
        return (long[]){"Str", v, "", "", "", "", "", "", 0};
    }

long n_bool(long v) {
        return (long[]){"Bool", v, "", "", "", "", "", "", 0};
    }

long n_ident(long v) {
        return (long[]){"Ident", v, "", "", "", "", "", "", 0};
    }

long n_bin(long op, long l, long r) {
        return (long[]){"Bin", "", op, l, r, "", "", "", 0};
    }

long n_unary(long op, long e) {
        return (long[]){"Unary", "", op, e, "", "", "", "", 0};
    }

long n_call(long callee, long call_args) {
        return (long[]){"Call", "", "", callee, "", "", "", "", call_args};
    }

long n_let(long name, long val) {
        return (long[]){"Let", name, "", val, "", "", "", "", 0};
    }

long n_assign(long name, long val) {
        return (long[]){"Assign", name, "", val, "", "", "", "", 0};
    }

long n_exprstmt(long e) {
        return (long[]){"ExprStmt", "", "", e, "", "", "", "", 0};
    }

long n_if(long c, long t, long e) {
        return (long[]){"If", "", "", "", "", c, t, e, 0};
    }

long n_while(long c, long b) {
        return (long[]){"While", "", "", "", "", c, b, "", 0};
    }

long n_for_range(long var_name, long lo, long hi, long body) {
        return (long[]){"ForRange", var_name, "", lo, hi, "", body, "", 0};
    }

long n_block(long stmts) {
        return (long[]){"Block", "", "", "", "", "", "", "", stmts};
    }

long n_fn(long name, long params, long body) {
        return (long[]){"Fn", name, "", "", body, "", "", "", params};
    }

long n_return(long e) {
        return (long[]){"Return", "", "", e, "", "", "", "", 0};
    }

long n_arrlit(long elems) {
        return (long[]){"ArrLit", "", "", "", "", "", "", "", elems};
    }

long n_index(long arr, long idx) {
        return (long[]){"Index", "", "", arr, idx, "", "", "", 0};
    }

long n_structdecl(long name, long field_names) {
        return (long[]){"StructDecl", name, "", "", "", "", "", "", field_names};
    }

long n_structlit(long name, long vals) {
        return (long[]){"StructLit", name, "", "", "", "", "", "", vals};
    }

long n_field(long obj, long name) {
        return (long[]){"Field", name, "", obj, "", "", "", "", 0};
    }

long n_method(long obj, long name, long m_args) {
        return (long[]){"Method", name, "", obj, "", "", "", "", m_args};
    }

long p_peek() {
        return p_toks[p_pos];
    }

long p_kind() {
        return /* unknown field kind */ 0;
    }

long p_advance() {
        long t = p_toks[p_pos];
        p_pos = (p_pos + 1);
        return t;
    }

long p_eat(long k) {
        if ((p_kind() == k)) {
            p_advance();
            return true;
        }
        return false;
    }

long p_skip_nl() {
        while ((p_kind() == "NL")) {
            p_advance();
        }
    }

long parse_primary() {
        long t = p_peek();
        if ((/* unknown field kind */ 0 == "Int")) {
            p_advance();
            return n_int(/* unknown field value */ 0);
        }
        if ((/* unknown field kind */ 0 == "Float")) {
            p_advance();
            return n_float(/* unknown field value */ 0);
        }
        if ((/* unknown field kind */ 0 == "Str")) {
            p_advance();
            return n_str(/* unknown field value */ 0);
        }
        if ((/* unknown field kind */ 0 == "True")) {
            p_advance();
            return n_bool("true");
        }
        if ((/* unknown field kind */ 0 == "False")) {
            p_advance();
            return n_bool("false");
        }
        if ((/* unknown field kind */ 0 == "Bang")) {
            p_advance();
            return n_unary("!", parse_primary());
        }
        if ((/* unknown field kind */ 0 == "Minus")) {
            p_advance();
            return n_unary("-", parse_primary());
        }
        if ((/* unknown field kind */ 0 == "Ident")) {
            p_advance();
            long node = n_ident(/* unknown field value */ 0);
            if ((((p_kind() == "LB") && (struct_field_index(/* unknown field value */ 0, "") >= (-10))) && (hexa_str_len(struct_lookup_fields(/* unknown field value */ 0)) > 0))) {
                p_advance();
                p_skip_nl();
                hexa_arr vals = hexa_arr_new();
                while (((p_kind() != "RB") && (p_kind() != "EOF"))) {
                    long _fname = /* unknown field value */ 0;
                    p_eat("Colon");
                    vals = hexa_arr_push(vals, parse_expr());
                    if ((p_kind() == "Comma")) {
                        p_advance();
                    }
                    p_skip_nl();
                }
                p_eat("RB");
                node = n_structlit(/* unknown field value */ 0, vals);
            } else {
                if ((p_kind() == "LP")) {
                    p_advance();
                    hexa_arr call_args = hexa_arr_new();
                    if ((p_kind() != "RP")) {
                        call_args = hexa_arr_push(call_args, parse_expr());
                        while (p_eat("Comma")) {
                            call_args = hexa_arr_push(call_args, parse_expr());
                        }
                    }
                    p_eat("RP");
                    node = n_call(n_ident(/* unknown field value */ 0), call_args);
                }
            }
            long cont = true;
            while (cont) {
                if ((p_kind() == "LSq")) {
                    p_advance();
                    long idx = parse_expr();
                    p_eat("RSq");
                    node = n_index(node, idx);
                } else {
                    if ((p_kind() == "Dot")) {
                        p_advance();
                        long fname = /* unknown field value */ 0;
                        if ((p_kind() == "LP")) {
                            p_advance();
                            hexa_arr m_args = hexa_arr_new();
                            if ((p_kind() != "RP")) {
                                m_args = hexa_arr_push(m_args, parse_expr());
                                while (p_eat("Comma")) {
                                    m_args = hexa_arr_push(m_args, parse_expr());
                                }
                            }
                            p_eat("RP");
                            node = n_method(node, fname, m_args);
                        } else {
                            node = n_field(node, fname);
                        }
                    } else {
                        cont = false;
                    }
                }
            }
            return node;
        }
        if ((/* unknown field kind */ 0 == "LP")) {
            p_advance();
            long e = parse_expr();
            p_eat("RP");
            return e;
        }
        if ((/* unknown field kind */ 0 == "LSq")) {
            p_advance();
            hexa_arr elems = hexa_arr_new();
            if ((p_kind() != "RSq")) {
                elems = hexa_arr_push(elems, parse_expr());
                while (p_eat("Comma")) {
                    elems = hexa_arr_push(elems, parse_expr());
                }
            }
            p_eat("RSq");
            return n_arrlit(elems);
        }
        printf("%s %ld %ld\n", "parse error: unexpected", (long)(/* unknown field kind */ 0), (long)(/* unknown field value */ 0));
        p_advance();
        return empty();
    }

long parse_mul() {
        long left = parse_primary();
        while ((((p_kind() == "Star") || (p_kind() == "Slash")) || (p_kind() == "Percent"))) {
            long op = /* unknown field value */ 0;
            long right = parse_primary();
            left = n_bin(op, left, right);
        }
        return left;
    }

long parse_add() {
        long left = parse_mul();
        while (((p_kind() == "Plus") || (p_kind() == "Minus"))) {
            long op = /* unknown field value */ 0;
            long right = parse_mul();
            left = n_bin(op, left, right);
        }
        return left;
    }

long parse_cmp() {
        long left = parse_add();
        while (((((((p_kind() == "EqEq") || (p_kind() == "NotEq")) || (p_kind() == "LT")) || (p_kind() == "LE")) || (p_kind() == "GT")) || (p_kind() == "GE"))) {
            long op = /* unknown field value */ 0;
            long right = parse_add();
            left = n_bin(op, left, right);
        }
        return left;
    }

long parse_and() {
        long left = parse_cmp();
        while ((p_kind() == "AndAnd")) {
            p_advance();
            long right = parse_cmp();
            left = n_bin("&&", left, right);
        }
        return left;
    }

long parse_or() {
        long left = parse_and();
        while ((p_kind() == "OrOr")) {
            p_advance();
            long right = parse_and();
            left = n_bin("||", left, right);
        }
        return left;
    }

long parse_expr() {
        return parse_or();
    }

long parse_block() {
        p_eat("LB");
        p_skip_nl();
        hexa_arr stmts = hexa_arr_new();
        while (((p_kind() != "RB") && (p_kind() != "EOF"))) {
            stmts = hexa_arr_push(stmts, parse_stmt());
            p_skip_nl();
        }
        p_eat("RB");
        return n_block(stmts);
    }

long parse_if() {
        p_eat("If");
        long c = parse_expr();
        long t = parse_block();
        p_skip_nl();
        long e = empty();
        if ((p_kind() == "Else")) {
            p_advance();
            p_skip_nl();
            if ((p_kind() == "If")) {
                long inner = parse_if();
                hexa_arr stmts = hexa_arr_new();
                stmts = hexa_arr_push(stmts, inner);
                e = n_block(stmts);
            } else {
                e = parse_block();
            }
        }
        return n_if(c, t, e);
    }

long parse_while() {
        p_eat("While");
        long c = parse_expr();
        long b = parse_block();
        return n_while(c, b);
    }

long parse_for() {
        p_eat("For");
        long var_name = /* unknown field value */ 0;
        p_eat("In");
        long lo = parse_expr();
        p_eat("DotDot");
        long hi = parse_expr();
        long b = parse_block();
        return n_for_range(var_name, lo, hi, b);
    }

long parse_struct() {
        p_eat("Struct");
        long name = /* unknown field value */ 0;
        p_eat("LB");
        p_skip_nl();
        hexa_arr fields = hexa_arr_new();
        while (((p_kind() != "RB") && (p_kind() != "EOF"))) {
            if ((p_kind() == "Ident")) {
                long fname = /* unknown field value */ 0;
                fields = hexa_arr_push(fields, fname);
                if ((p_kind() == "Colon")) {
                    p_advance();
                    if ((p_kind() == "Ident")) {
                        p_advance();
                    }
                }
                if ((p_kind() == "Comma")) {
                    p_advance();
                }
                p_skip_nl();
            } else {
                p_advance();
            }
        }
        p_eat("RB");
        struct_register(name, fields);
        return n_structdecl(name, fields);
    }

long parse_fn() {
        p_eat("Fn");
        long name = /* unknown field value */ 0;
        p_eat("LP");
        hexa_arr params = hexa_arr_new();
        if ((p_kind() != "RP")) {
            params = hexa_arr_push(params, /* unknown field value */ 0);
            while (p_eat("Comma")) {
                params = hexa_arr_push(params, /* unknown field value */ 0);
            }
        }
        p_eat("RP");
        long body = parse_block();
        return n_fn(name, params, body);
    }

long parse_stmt() {
        p_skip_nl();
        long k = p_kind();
        if ((k == "Let")) {
            p_advance();
            if ((p_kind() == "Mut")) {
                p_advance();
            }
            long name = /* unknown field value */ 0;
            p_eat("Eq");
            long val = parse_expr();
            return n_let(name, val);
        }
        if ((k == "Struct")) {
            return parse_struct();
        }
        if ((k == "Fn")) {
            return parse_fn();
        }
        if ((k == "Return")) {
            p_advance();
            long e = parse_expr();
            return n_return(e);
        }
        if ((k == "If")) {
            return parse_if();
        }
        if ((k == "While")) {
            return parse_while();
        }
        if ((k == "For")) {
            return parse_for();
        }
        if ((k == "Ident")) {
            long next_k = /* unknown field kind */ 0;
            if ((next_k == "Eq")) {
                long name = /* unknown field value */ 0;
                p_advance();
                long val = parse_expr();
                return n_assign(name, val);
            }
            if (((((next_k == "PlusEq") || (next_k == "MinusEq")) || (next_k == "StarEq")) || (next_k == "SlashEq"))) {
                long name = /* unknown field value */ 0;
                long op_tok = p_advance();
                long rhs = parse_expr();
                const char* bin_op = hexa_substr(/* unknown field value */ 0, 0, 1);
                long new_rhs = n_bin(bin_op, n_ident(name), rhs);
                return n_assign(name, new_rhs);
            }
        }
        long e = parse_expr();
        return n_exprstmt(e);
    }

long parse_program(long toks) {
        p_toks = toks;
        p_pos = 0;
        hexa_arr stmts = hexa_arr_new();
        p_skip_nl();
        while ((p_kind() != "EOF")) {
            stmts = hexa_arr_push(stmts, parse_stmt());
            p_skip_nl();
        }
        return stmts;
    }

long struct_register(long name, long fields) {
        struct_tab_names = hexa_arr_push(struct_tab_names, name);
        struct_tab_fields = hexa_arr_push(struct_tab_fields, fields);
    }

long struct_lookup_fields(long name) {
        long i = 0;
        while ((i < hexa_str_len(struct_tab_names))) {
            if ((struct_tab_names[i] == name)) {
                return struct_tab_fields[i];
            }
            i = (i + 1);
        }
        return 0;
    }

long struct_field_index(long struct_name, long field_name) {
        long fields = struct_lookup_fields(struct_name);
        long i = 0;
        while ((i < fields.n)) {
            if ((fields.d[i] == field_name)) {
                return i;
            }
            i = (i + 1);
        }
        return (-1);
    }

long env_lookup(long name) {
        long i = 0;
        while ((i < hexa_str_len(env_names))) {
            if ((env_names[i] == name)) {
                return env_types[i];
            }
            i = (i + 1);
        }
        return "Int";
    }

long env_lookup_len(long name) {
        long i = 0;
        while ((i < hexa_str_len(env_names))) {
            if ((env_names[i] == name)) {
                return env_lens[i];
            }
            i = (i + 1);
        }
        return (-1);
    }

long env_set(long name, long ty) {
        env_names = hexa_arr_push(env_names, name);
        env_types = hexa_arr_push(env_types, ty);
        env_lens = hexa_arr_push(env_lens, (-1));
    }

long env_set_arr(long name, long alen) {
        env_names = hexa_arr_push(env_names, name);
        env_types = hexa_arr_push(env_types, "Arr");
        env_lens = hexa_arr_push(env_lens, alen);
    }

long env_set_struct(long name, long struct_name) {
        env_names = hexa_arr_push(env_names, name);
        env_types = hexa_arr_push(env_types, struct_name);
        long fields = struct_lookup_fields(struct_name);
        env_lens = hexa_arr_push(env_lens, fields.n);
    }

long type_of(long *node) {
        if ((/* unknown field kind */ 0 == "Int")) {
            return "Int";
        }
        if ((/* unknown field kind */ 0 == "Float")) {
            return "Float";
        }
        if ((/* unknown field kind */ 0 == "Str")) {
            return "Str";
        }
        if ((/* unknown field kind */ 0 == "Bool")) {
            return "Int";
        }
        if ((/* unknown field kind */ 0 == "Ident")) {
            return env_lookup(/* unknown field value */ 0);
        }
        if ((/* unknown field kind */ 0 == "Bin")) {
            long lt = type_of(/* unknown field left */ 0);
            long rt = type_of(/* unknown field right */ 0);
            if (((/* unknown field op */ 0 == "+") && ((lt == "Str") || (rt == "Str")))) {
                return "Str";
            }
            if (((lt == "Float") || (rt == "Float"))) {
                return "Float";
            }
            return "Int";
        }
        if ((/* unknown field kind */ 0 == "Unary")) {
            return type_of(/* unknown field left */ 0);
        }
        if ((/* unknown field kind */ 0 == "Call")) {
            long cl = /* unknown field left */ 0;
            if ((/* unknown field kind */ 0 == "Ident")) {
                if ((/* unknown field value */ 0 == "read_file")) {
                    return "Str";
                }
                if ((/* unknown field value */ 0 == "to_string")) {
                    return "Str";
                }
                if ((/* unknown field value */ 0 == "args")) {
                    return "DynArr";
                }
            }
            return "Int";
        }
        if ((/* unknown field kind */ 0 == "StructLit")) {
            return /* unknown field value */ 0;
        }
        if ((/* unknown field kind */ 0 == "Field")) {
            return "Int";
        }
        if ((/* unknown field kind */ 0 == "Index")) {
            return "Int";
        }
        if ((/* unknown field kind */ 0 == "Method")) {
            if ((/* unknown field value */ 0 == "substring")) {
                return "Str";
            }
            if ((/* unknown field value */ 0 == "len")) {
                return "Int";
            }
            if ((/* unknown field value */ 0 == "push")) {
                return "DynArr";
            }
            if ((/* unknown field value */ 0 == "chars")) {
                return "DynArr";
            }
            return "Int";
        }
        return "Int";
    }

long coerce_str(long node, long t) {
        if ((t == "Str")) {
            return emit_expr(node);
        }
        return hexa_concat(hexa_concat("hexa_int_to_str((long)(", hexa_int_to_str((long)(emit_expr(node)))), "))");
    }

long emit_expr(long *node) {
        if ((/* unknown field kind */ 0 == "Int")) {
            return /* unknown field value */ 0;
        }
        if ((/* unknown field kind */ 0 == "Float")) {
            return /* unknown field value */ 0;
        }
        if ((/* unknown field kind */ 0 == "Bool")) {
            return /* unknown field value */ 0;
        }
        if ((/* unknown field kind */ 0 == "Str")) {
            return "\";
            " + node.value + ";
            "";
        }
        if ((/* unknown field kind */ 0 == "Ident")) {
            return /* unknown field value */ 0;
        }
        if ((/* unknown field kind */ 0 == "Bin")) {
            if ((/* unknown field op */ 0 == "+")) {
                long lt = type_of(/* unknown field left */ 0);
                long rt = type_of(/* unknown field right */ 0);
                if (((lt == "Str") || (rt == "Str"))) {
                    long l_code = coerce_str(/* unknown field left */ 0, lt);
                    long r_code = coerce_str(/* unknown field right */ 0, rt);
                    return hexa_concat(hexa_concat(hexa_concat(hexa_concat("hexa_concat(", hexa_int_to_str((long)(l_code))), ", "), hexa_int_to_str((long)(r_code))), ")");
                }
            }
            if (((/* unknown field op */ 0 == "==") || (/* unknown field op */ 0 == "!="))) {
                long lt = type_of(/* unknown field left */ 0);
                long rt = type_of(/* unknown field right */ 0);
                if (((lt == "Str") && (rt == "Str"))) {
                    const char* cmp = hexa_concat(hexa_concat(hexa_concat(hexa_concat("strcmp(", hexa_int_to_str((long)(emit_expr(/* unknown field left */ 0)))), ", "), hexa_int_to_str((long)(emit_expr(/* unknown field right */ 0)))), ")");
                    if ((/* unknown field op */ 0 == "==")) {
                        return hexa_concat(hexa_concat("(", cmp), " == 0)");
                    }
                    return hexa_concat(hexa_concat("(", cmp), " != 0)");
                }
            }
            return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("(", hexa_int_to_str((long)(emit_expr(/* unknown field left */ 0)))), " "), hexa_int_to_str((long)(/* unknown field op */ 0))), " "), hexa_int_to_str((long)(emit_expr(/* unknown field right */ 0)))), ")");
        }
        if ((/* unknown field kind */ 0 == "Unary")) {
            return hexa_concat(hexa_concat(hexa_concat("(", hexa_int_to_str((long)(/* unknown field op */ 0))), hexa_int_to_str((long)(emit_expr(/* unknown field left */ 0)))), ")");
        }
        if ((/* unknown field kind */ 0 == "Index")) {
            long obj = /* unknown field left */ 0;
            if (((/* unknown field kind */ 0 == "Ident") && (env_lookup(/* unknown field value */ 0) == "DynArr"))) {
                return hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(/* unknown field value */ 0)), ".d["), hexa_int_to_str((long)(emit_expr(/* unknown field right */ 0)))), "]");
            }
            return hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(emit_expr(/* unknown field left */ 0))), "["), hexa_int_to_str((long)(emit_expr(/* unknown field right */ 0)))), "]");
        }
        if ((/* unknown field kind */ 0 == "Method")) {
            long obj_code = emit_expr(/* unknown field left */ 0);
            long mname = /* unknown field value */ 0;
            if ((mname == "substring")) {
                long a = emit_expr(/* unknown field args */ 0[0]);
                long b = emit_expr(/* unknown field args */ 0[1]);
                return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("hexa_substr(", hexa_int_to_str((long)(obj_code))), ", "), hexa_int_to_str((long)(a))), ", "), hexa_int_to_str((long)(b))), ")");
            }
            if ((mname == "len")) {
                long lo = /* unknown field left */ 0;
                if (((/* unknown field kind */ 0 == "Ident") && (env_lookup(/* unknown field value */ 0) == "DynArr"))) {
                    return hexa_concat(hexa_int_to_str((long)(/* unknown field value */ 0)), ".n");
                }
                return hexa_concat(hexa_concat("hexa_str_len(", hexa_int_to_str((long)(obj_code))), ")");
            }
            if ((mname == "push")) {
                return hexa_concat(hexa_concat(hexa_concat(hexa_concat("hexa_arr_push(", hexa_int_to_str((long)(obj_code))), ", "), hexa_int_to_str((long)(emit_expr(/* unknown field args */ 0[0])))), ")");
            }
            if ((mname == "chars")) {
                return hexa_concat(hexa_concat("hexa_chars(", hexa_int_to_str((long)(obj_code))), ")");
            }
            return hexa_concat(hexa_concat("/* unsupported method .", hexa_int_to_str((long)(mname))), " */ 0");
        }
        if ((/* unknown field kind */ 0 == "Field")) {
            long obj = /* unknown field left */ 0;
            const char* sname = "";
            if ((/* unknown field kind */ 0 == "Ident")) {
                sname = env_lookup(/* unknown field value */ 0);
            }
            if ((/* unknown field kind */ 0 == "StructLit")) {
                sname = /* unknown field value */ 0;
            }
            long idx = struct_field_index(sname, /* unknown field value */ 0);
            if ((idx < 0)) {
                return hexa_concat(hexa_concat("/* unknown field ", hexa_int_to_str((long)(/* unknown field value */ 0))), " */ 0");
            }
            return hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(emit_expr(obj))), "["), hexa_int_to_str((long)(idx))), "]");
        }
        if ((/* unknown field kind */ 0 == "StructLit")) {
            const char* s = "(long[]){";
            long i = 0;
            while ((i < hexa_str_len(/* unknown field args */ 0))) {
                if ((i > 0)) {
                    s = hexa_concat(hexa_int_to_str((long)(s)), ", ");
                }
                s = (s + emit_expr(/* unknown field args */ 0[i]));
                i = (i + 1);
            }
            s = hexa_concat(hexa_int_to_str((long)(s)), "}");
            return s;
        }
        if ((/* unknown field kind */ 0 == "Call")) {
            long callee = /* unknown field left */ 0;
            if (((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == "println"))) {
                return emit_println(/* unknown field args */ 0);
            }
            if (((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == "len"))) {
                if (((hexa_str_len(/* unknown field args */ 0) == 1) && (/* unknown field kind */ 0 == "Ident"))) {
                    long vn = /* unknown field value */ 0;
                    long vt = env_lookup(vn);
                    if ((vt == "DynArr")) {
                        return hexa_concat(hexa_int_to_str((long)(vn)), ".n");
                    }
                    long alen = env_lookup_len(vn);
                    if ((alen >= 0)) {
                        return hexa_int_to_str((long)(alen));
                    }
                }
                return hexa_concat(hexa_concat("hexa_str_len(", hexa_int_to_str((long)(emit_expr(/* unknown field args */ 0[0])))), ")");
            }
            if (((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == "read_file"))) {
                return hexa_concat(hexa_concat("hexa_read_file(", hexa_int_to_str((long)(emit_expr(/* unknown field args */ 0[0])))), ")");
            }
            if (((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == "write_file"))) {
                return hexa_concat(hexa_concat(hexa_concat(hexa_concat("hexa_write_file(", hexa_int_to_str((long)(emit_expr(/* unknown field args */ 0[0])))), ", "), hexa_int_to_str((long)(emit_expr(/* unknown field args */ 0[1])))), ")");
            }
            if (((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == "to_string"))) {
                long t = type_of(/* unknown field args */ 0[0]);
                if ((t == "Str")) {
                    return emit_expr(/* unknown field args */ 0[0]);
                }
                return hexa_concat(hexa_concat("hexa_int_to_str((long)(", hexa_int_to_str((long)(emit_expr(/* unknown field args */ 0[0])))), "))");
            }
            if (((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == "is_alpha"))) {
                return hexa_concat(hexa_concat("hexa_is_alpha(", hexa_int_to_str((long)(emit_expr(/* unknown field args */ 0[0])))), ")");
            }
            if (((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == "is_alphanumeric"))) {
                return hexa_concat(hexa_concat("hexa_is_alnum(", hexa_int_to_str((long)(emit_expr(/* unknown field args */ 0[0])))), ")");
            }
            if (((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == "args"))) {
                return "hexa_args()";
            }
            if ((/* unknown field kind */ 0 == "Ident")) {
                const char* s = hexa_concat(hexa_int_to_str((long)(/* unknown field value */ 0)), "(");
                long i = 0;
                while ((i < hexa_str_len(/* unknown field args */ 0))) {
                    if ((i > 0)) {
                        s = hexa_concat(hexa_int_to_str((long)(s)), ", ");
                    }
                    s = (s + emit_expr(/* unknown field args */ 0[i]));
                    i = (i + 1);
                }
                s = hexa_concat(hexa_int_to_str((long)(s)), ")");
                return s;
            }
            return "/* unsupported call */ 0";
        }
        return "0";
    }

long emit_println(hexa_arr pl_args) {
        const char* fmt = "";
        const char* vals = "";
        long i = 0;
        while ((i < pl_args.n)) {
            if ((i > 0)) {
                fmt = hexa_concat(fmt, " ");
            }
            long a = pl_args.d[i];
            long t = type_of(a);
            if ((t == "Str")) {
                fmt = hexa_concat(fmt, "%s");
            } else {
                if ((t == "Float")) {
                    fmt = hexa_concat(fmt, "%g");
                } else {
                    fmt = hexa_concat(fmt, "%ld");
                }
            }
            if ((hexa_str_len(vals) > 0)) {
                vals = hexa_concat(hexa_int_to_str((long)(vals)), ", ");
            }
            if ((t == "Str")) {
                vals = (vals + emit_expr(a));
            } else {
                if ((t == "Float")) {
                    vals = hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(vals)), "(double)("), hexa_int_to_str((long)(emit_expr(a)))), ")");
                } else {
                    vals = hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(vals)), "(long)("), hexa_int_to_str((long)(emit_expr(a)))), ")");
                }
            }
            i = (i + 1);
        }
        fmt = hexa_concat(fmt, "\\n");
        if ((pl_args.n == 0)) {
            return "printf(\";
            n;
            ")";
        }
        return "printf(\";
        " + fmt + ";
        hexa_concat(hexa_concat(", ", hexa_int_to_str((long)(vals))), ")");
    }

long pad() {
        const char* s = "";
        long k = 0;
        while ((k < indent_level)) {
            s = hexa_concat(hexa_int_to_str((long)(s)), "    ");
            k = (k + 1);
        }
        return s;
    }

long emit_stmt(long *node) {
        if ((/* unknown field kind */ 0 == "StructDecl")) {
            return "";
        }
        if ((/* unknown field kind */ 0 == "Let")) {
            long val = /* unknown field left */ 0;
            if ((/* unknown field kind */ 0 == "ArrLit")) {
                env_names = hexa_arr_push(env_names, /* unknown field value */ 0);
                env_types = hexa_arr_push(env_types, "DynArr");
                env_lens = hexa_arr_push(env_lens, (-1));
                long n_items = hexa_str_len(/* unknown field args */ 0);
                if ((n_items == 0)) {
                    return hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(pad())), "hexa_arr "), hexa_int_to_str((long)(/* unknown field value */ 0))), " = hexa_arr_new();\n");
                }
                const char* s = hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(pad())), "hexa_arr "), hexa_int_to_str((long)(/* unknown field value */ 0))), " = hexa_arr_lit((long[]){");
                long i = 0;
                while ((i < n_items)) {
                    if ((i > 0)) {
                        s = hexa_concat(hexa_int_to_str((long)(s)), ", ");
                    }
                    s = (s + emit_expr(/* unknown field args */ 0[i]));
                    i = (i + 1);
                }
                s = hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(s)), "}, "), hexa_int_to_str((long)(n_items))), ");\n");
                return s;
            }
            if ((/* unknown field kind */ 0 == "StructLit")) {
                env_set_struct(/* unknown field value */ 0, /* unknown field value */ 0);
                const char* s = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(pad())), "long "), hexa_int_to_str((long)(/* unknown field value */ 0))), "["), hexa_int_to_str((long)(hexa_str_len(/* unknown field args */ 0)))), "] = {");
                long i = 0;
                while ((i < hexa_str_len(/* unknown field args */ 0))) {
                    if ((i > 0)) {
                        s = hexa_concat(hexa_int_to_str((long)(s)), ", ");
                    }
                    s = (s + emit_expr(/* unknown field args */ 0[i]));
                    i = (i + 1);
                }
                s = hexa_concat(hexa_int_to_str((long)(s)), "};\n");
                return s;
            }
            long t = type_of(val);
            env_set(/* unknown field value */ 0, t);
            if ((t == "Str")) {
                return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(pad())), "const char* "), hexa_int_to_str((long)(/* unknown field value */ 0))), " = "), hexa_int_to_str((long)(emit_expr(val)))), ";\n");
            }
            if ((t == "Float")) {
                return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(pad())), "double "), hexa_int_to_str((long)(/* unknown field value */ 0))), " = "), hexa_int_to_str((long)(emit_expr(val)))), ";\n");
            }
            if ((t == "DynArr")) {
                return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(pad())), "hexa_arr "), hexa_int_to_str((long)(/* unknown field value */ 0))), " = "), hexa_int_to_str((long)(emit_expr(val)))), ";\n");
            }
            return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(pad())), "long "), hexa_int_to_str((long)(/* unknown field value */ 0))), " = "), hexa_int_to_str((long)(emit_expr(val)))), ";\n");
        }
        if ((/* unknown field kind */ 0 == "Assign")) {
            return hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)((pad() + /* unknown field value */ 0))), " = "), hexa_int_to_str((long)(emit_expr(/* unknown field left */ 0)))), ";\n");
        }
        if ((/* unknown field kind */ 0 == "ExprStmt")) {
            return hexa_concat(hexa_int_to_str((long)((pad() + emit_expr(/* unknown field left */ 0)))), ";\n");
        }
        if ((/* unknown field kind */ 0 == "If")) {
            const char* out = hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(pad())), "if ("), hexa_int_to_str((long)(emit_expr(/* unknown field cond */ 0)))), ") ");
            out = hexa_concat(out, hexa_int_to_str((long)(emit_block(/* unknown field then_b */ 0))));
            if ((/* unknown field kind */ 0 == "Block")) {
                out = hexa_concat(hexa_concat(out, " else "), hexa_int_to_str((long)(emit_block(/* unknown field else_b */ 0))));
            }
            out = hexa_concat(out, "\n");
            return out;
        }
        if ((/* unknown field kind */ 0 == "While")) {
            return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(pad())), "while ("), hexa_int_to_str((long)(emit_expr(/* unknown field cond */ 0)))), ") "), hexa_int_to_str((long)(emit_block(/* unknown field then_b */ 0)))), "\n");
        }
        if ((/* unknown field kind */ 0 == "ForRange")) {
            long v = /* unknown field value */ 0;
            env_set(v, "Int");
            long lo = emit_expr(/* unknown field left */ 0);
            long hi = emit_expr(/* unknown field right */ 0);
            return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(pad())), "for (long "), hexa_int_to_str((long)(v))), " = "), hexa_int_to_str((long)(lo))), "; "), hexa_int_to_str((long)(v))), " < "), hexa_int_to_str((long)(hi))), "; "), hexa_int_to_str((long)(v))), "++) "), hexa_int_to_str((long)(emit_block(/* unknown field then_b */ 0)))), "\n");
        }
        if ((/* unknown field kind */ 0 == "Return")) {
            return hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(pad())), "return "), hexa_int_to_str((long)(emit_expr(/* unknown field left */ 0)))), ";\n");
        }
        return hexa_concat(hexa_int_to_str((long)(pad())), "/* unsupported stmt */\n");
    }

long find_method_on_param(long *node, long pname, long mname) {
        if ((/* unknown field kind */ 0 == "Method")) {
            long obj = /* unknown field left */ 0;
            if ((((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == pname)) && (/* unknown field value */ 0 == mname))) {
                return true;
            }
        }
        if (((/* unknown field kind */ 0 == "Bin") || (/* unknown field kind */ 0 == "Index"))) {
            if (find_method_on_param(/* unknown field left */ 0, pname, mname)) {
                return true;
            }
            return find_method_on_param(/* unknown field right */ 0, pname, mname);
        }
        if ((((((((/* unknown field kind */ 0 == "Unary") || (/* unknown field kind */ 0 == "ExprStmt")) || (/* unknown field kind */ 0 == "Return")) || (/* unknown field kind */ 0 == "Let")) || (/* unknown field kind */ 0 == "Assign")) || (/* unknown field kind */ 0 == "Field")) || (/* unknown field kind */ 0 == "Method"))) {
            if (find_method_on_param(/* unknown field left */ 0, pname, mname)) {
                return true;
            }
        }
        if ((/* unknown field kind */ 0 == "If")) {
            if (find_method_on_param(/* unknown field cond */ 0, pname, mname)) {
                return true;
            }
            if (find_method_on_param(/* unknown field then_b */ 0, pname, mname)) {
                return true;
            }
            return find_method_on_param(/* unknown field else_b */ 0, pname, mname);
        }
        if (((/* unknown field kind */ 0 == "While") || (/* unknown field kind */ 0 == "ForRange"))) {
            if (find_method_on_param(/* unknown field cond */ 0, pname, mname)) {
                return true;
            }
            return find_method_on_param(/* unknown field then_b */ 0, pname, mname);
        }
        if ((/* unknown field kind */ 0 == "Block")) {
            long stmts = /* unknown field args */ 0;
            long i = 0;
            while ((i < hexa_str_len(stmts))) {
                if (find_method_on_param(stmts[i], pname, mname)) {
                    return true;
                }
                i = (i + 1);
            }
        }
        if (((/* unknown field kind */ 0 == "Call") || (/* unknown field kind */ 0 == "Method"))) {
            long a2 = /* unknown field args */ 0;
            long i = 0;
            while ((i < hexa_str_len(a2))) {
                if (find_method_on_param(a2[i], pname, mname)) {
                    return true;
                }
                i = (i + 1);
            }
        }
        return false;
    }

long find_param_field(long *node, long pname) {
        if ((/* unknown field kind */ 0 == "Field")) {
            long obj = /* unknown field left */ 0;
            if (((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == pname))) {
                long fname = /* unknown field value */ 0;
                long si = 0;
                while ((si < hexa_str_len(struct_tab_names))) {
                    long sname = struct_tab_names[si];
                    if ((struct_field_index(sname, fname) >= 0)) {
                        return sname;
                    }
                    si = (si + 1);
                }
            }
        }
        if (((/* unknown field kind */ 0 == "Bin") || (/* unknown field kind */ 0 == "Index"))) {
            long l = find_param_field(/* unknown field left */ 0, pname);
            if ((l != "")) {
                return l;
            }
            return find_param_field(/* unknown field right */ 0, pname);
        }
        if (((((((/* unknown field kind */ 0 == "Unary") || (/* unknown field kind */ 0 == "Field")) || (/* unknown field kind */ 0 == "ExprStmt")) || (/* unknown field kind */ 0 == "Return")) || (/* unknown field kind */ 0 == "Let")) || (/* unknown field kind */ 0 == "Assign"))) {
            return find_param_field(/* unknown field left */ 0, pname);
        }
        if ((/* unknown field kind */ 0 == "If")) {
            long c = find_param_field(/* unknown field cond */ 0, pname);
            if ((c != "")) {
                return c;
            }
            long t = find_param_field(/* unknown field then_b */ 0, pname);
            if ((t != "")) {
                return t;
            }
            return find_param_field(/* unknown field else_b */ 0, pname);
        }
        if ((/* unknown field kind */ 0 == "While")) {
            long c = find_param_field(/* unknown field cond */ 0, pname);
            if ((c != "")) {
                return c;
            }
            return find_param_field(/* unknown field then_b */ 0, pname);
        }
        if ((/* unknown field kind */ 0 == "Block")) {
            long stmts = /* unknown field args */ 0;
            long i = 0;
            while ((i < hexa_str_len(stmts))) {
                long r = find_param_field(stmts[i], pname);
                if ((r != "")) {
                    return r;
                }
                i = (i + 1);
            }
        }
        if ((/* unknown field kind */ 0 == "Call")) {
            long args2 = /* unknown field args */ 0;
            long i = 0;
            while ((i < hexa_str_len(args2))) {
                long r = find_param_field(args2[i], pname);
                if ((r != "")) {
                    return r;
                }
                i = (i + 1);
            }
        }
        return "";
    }

long find_param_indexed(long *node, long pname) {
        if ((/* unknown field kind */ 0 == "Index")) {
            long obj = /* unknown field left */ 0;
            if (((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == pname))) {
                return true;
            }
        }
        if (((/* unknown field kind */ 0 == "Bin") || (/* unknown field kind */ 0 == "Index"))) {
            if (find_param_indexed(/* unknown field left */ 0, pname)) {
                return true;
            }
            return find_param_indexed(/* unknown field right */ 0, pname);
        }
        if ((((((((/* unknown field kind */ 0 == "Unary") || (/* unknown field kind */ 0 == "ExprStmt")) || (/* unknown field kind */ 0 == "Return")) || (/* unknown field kind */ 0 == "Let")) || (/* unknown field kind */ 0 == "Assign")) || (/* unknown field kind */ 0 == "Field")) || (/* unknown field kind */ 0 == "Method"))) {
            if (find_param_indexed(/* unknown field left */ 0, pname)) {
                return true;
            }
        }
        if ((/* unknown field kind */ 0 == "If")) {
            if (find_param_indexed(/* unknown field cond */ 0, pname)) {
                return true;
            }
            if (find_param_indexed(/* unknown field then_b */ 0, pname)) {
                return true;
            }
            return find_param_indexed(/* unknown field else_b */ 0, pname);
        }
        if (((/* unknown field kind */ 0 == "While") || (/* unknown field kind */ 0 == "ForRange"))) {
            if (find_param_indexed(/* unknown field cond */ 0, pname)) {
                return true;
            }
            return find_param_indexed(/* unknown field then_b */ 0, pname);
        }
        if ((/* unknown field kind */ 0 == "Block")) {
            long stmts = /* unknown field args */ 0;
            long i = 0;
            while ((i < hexa_str_len(stmts))) {
                if (find_param_indexed(stmts[i], pname)) {
                    return true;
                }
                i = (i + 1);
            }
        }
        if (((/* unknown field kind */ 0 == "Call") || (/* unknown field kind */ 0 == "Method"))) {
            long a2 = /* unknown field args */ 0;
            long i = 0;
            while ((i < hexa_str_len(a2))) {
                if (find_param_indexed(a2[i], pname)) {
                    return true;
                }
                i = (i + 1);
            }
        }
        return false;
    }

long find_len_call_on_param(long *node, long pname) {
        if ((/* unknown field kind */ 0 == "Call")) {
            long c = /* unknown field left */ 0;
            if ((((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == "len")) && (hexa_str_len(/* unknown field args */ 0) == 1))) {
                long a0 = /* unknown field args */ 0[0];
                if (((/* unknown field kind */ 0 == "Ident") && (/* unknown field value */ 0 == pname))) {
                    return true;
                }
            }
        }
        if (((/* unknown field kind */ 0 == "Bin") || (/* unknown field kind */ 0 == "Index"))) {
            if (find_len_call_on_param(/* unknown field left */ 0, pname)) {
                return true;
            }
            return find_len_call_on_param(/* unknown field right */ 0, pname);
        }
        if ((((((((/* unknown field kind */ 0 == "Unary") || (/* unknown field kind */ 0 == "ExprStmt")) || (/* unknown field kind */ 0 == "Return")) || (/* unknown field kind */ 0 == "Let")) || (/* unknown field kind */ 0 == "Assign")) || (/* unknown field kind */ 0 == "Field")) || (/* unknown field kind */ 0 == "Method"))) {
            if (find_len_call_on_param(/* unknown field left */ 0, pname)) {
                return true;
            }
        }
        if ((/* unknown field kind */ 0 == "If")) {
            if (find_len_call_on_param(/* unknown field cond */ 0, pname)) {
                return true;
            }
            if (find_len_call_on_param(/* unknown field then_b */ 0, pname)) {
                return true;
            }
            return find_len_call_on_param(/* unknown field else_b */ 0, pname);
        }
        if (((/* unknown field kind */ 0 == "While") || (/* unknown field kind */ 0 == "ForRange"))) {
            if (find_len_call_on_param(/* unknown field cond */ 0, pname)) {
                return true;
            }
            return find_len_call_on_param(/* unknown field then_b */ 0, pname);
        }
        if ((/* unknown field kind */ 0 == "Block")) {
            long stmts = /* unknown field args */ 0;
            long i = 0;
            while ((i < hexa_str_len(stmts))) {
                if (find_len_call_on_param(stmts[i], pname)) {
                    return true;
                }
                i = (i + 1);
            }
        }
        if (((/* unknown field kind */ 0 == "Call") || (/* unknown field kind */ 0 == "Method"))) {
            long a2 = /* unknown field args */ 0;
            long i = 0;
            while ((i < hexa_str_len(a2))) {
                if (find_len_call_on_param(a2[i], pname)) {
                    return true;
                }
                i = (i + 1);
            }
        }
        return false;
    }

long infer_param_c_type(long body, long pname) {
        long sname = find_param_field(body, pname);
        if ((strcmp(sname, "") != 0)) {
            return hexa_concat("struct:", sname);
        }
        if (find_method_on_param(body, pname, "push")) {
            return "dynarr";
        }
        if ((find_len_call_on_param(body, pname) && find_param_indexed(body, pname))) {
            return "dynarr";
        }
        if (find_method_on_param(body, pname, "substring")) {
            return "str";
        }
        if (find_method_on_param(body, pname, "len")) {
            return "str";
        }
        return "int";
    }

long emit_fn_decl(long *node) {
        const char* s = hexa_concat(hexa_concat("long ", hexa_int_to_str((long)(/* unknown field value */ 0))), "(");
        long params = /* unknown field args */ 0;
        long i = 0;
        while ((i < hexa_str_len(params))) {
            if ((i > 0)) {
                s = hexa_concat(hexa_int_to_str((long)(s)), ", ");
            }
            long pname = params[i];
            long ty = infer_param_c_type(/* unknown field right */ 0, pname);
            if ((ty == "dynarr")) {
                s = hexa_concat(hexa_concat(hexa_int_to_str((long)(s)), "hexa_arr "), hexa_int_to_str((long)(pname)));
                env_names = hexa_arr_push(env_names, pname);
                env_types = hexa_arr_push(env_types, "DynArr");
                env_lens = hexa_arr_push(env_lens, (-1));
            } else {
                if ((ty == "str")) {
                    s = hexa_concat(hexa_concat(hexa_int_to_str((long)(s)), "const char* "), hexa_int_to_str((long)(pname)));
                    env_set(pname, "Str");
                } else {
                    if ((strcmp(hexa_substr(ty, 0, 7), "struct:") == 0)) {
                        const char* sn = hexa_substr(ty, 7, hexa_str_len(ty));
                        s = hexa_concat(hexa_concat(hexa_int_to_str((long)(s)), "long *"), hexa_int_to_str((long)(pname)));
                        env_set_struct(pname, sn);
                    } else {
                        s = hexa_concat(hexa_concat(hexa_int_to_str((long)(s)), "long "), hexa_int_to_str((long)(pname)));
                        env_set(pname, "Int");
                    }
                }
            }
            i = (i + 1);
        }
        s = hexa_concat(hexa_int_to_str((long)(s)), ") ");
        s = hexa_concat(hexa_int_to_str((long)((s + emit_block(/* unknown field right */ 0)))), "\n\n");
        return s;
    }

long emit_fn_proto(long *node) {
        const char* s = hexa_concat(hexa_concat("long ", hexa_int_to_str((long)(/* unknown field value */ 0))), "(");
        long params = /* unknown field args */ 0;
        long i = 0;
        while ((i < hexa_str_len(params))) {
            if ((i > 0)) {
                s = hexa_concat(hexa_int_to_str((long)(s)), ", ");
            }
            long pname = params[i];
            long ty = infer_param_c_type(/* unknown field right */ 0, pname);
            if ((ty == "dynarr")) {
                s = hexa_concat(hexa_concat(hexa_int_to_str((long)(s)), "hexa_arr "), hexa_int_to_str((long)(pname)));
            } else {
                if ((ty == "str")) {
                    s = hexa_concat(hexa_concat(hexa_int_to_str((long)(s)), "const char* "), hexa_int_to_str((long)(pname)));
                } else {
                    if ((strcmp(hexa_substr(ty, 0, 7), "struct:") == 0)) {
                        s = hexa_concat(hexa_concat(hexa_int_to_str((long)(s)), "long *"), hexa_int_to_str((long)(pname)));
                    } else {
                        s = hexa_concat(hexa_concat(hexa_int_to_str((long)(s)), "long "), hexa_int_to_str((long)(pname)));
                    }
                }
            }
            i = (i + 1);
        }
        s = hexa_concat(hexa_int_to_str((long)(s)), ");\n");
        return s;
    }

long emit_block(long *node) {
        const char* out = "{\n";
        indent_level = (indent_level + 1);
        long stmts = /* unknown field args */ 0;
        long i = 0;
        while ((i < hexa_str_len(stmts))) {
            out = hexa_concat(out, hexa_int_to_str((long)(emit_stmt(stmts[i]))));
            i = (i + 1);
        }
        indent_level = (indent_level - 1);
        out = hexa_concat(hexa_concat(out, hexa_int_to_str((long)(pad()))), "}");
        return out;
    }

long emit_runtime() {
        const char* r = "";
        r = hexa_concat(hexa_int_to_str((long)(r)), "#include <stdio.h>\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "#include <string.h>\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "#include <stdlib.h>\n\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static char hexa_arena[65536];\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static size_t hexa_arena_p = 0;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static char* hexa_alloc(size_t n) {\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    if (hexa_arena_p + n >= sizeof(hexa_arena)) { fprintf(stderr, \");
        hexa_arena;
        overflow;
        n;
        "); exit(1); }\n";
        r = hexa_concat(hexa_int_to_str((long)(r)), "    char* p = &hexa_arena[hexa_arena_p];\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    hexa_arena_p += n;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    return p;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "}\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static const char* hexa_concat(const char* a, const char* b) {\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    size_t la = strlen(a), lb = strlen(b);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    char* p = hexa_alloc(la + lb + 1);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    memcpy(p, a, la); memcpy(p + la, b, lb); p[la + lb] = 0;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    return p;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "}\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static const char* hexa_int_to_str(long v) {\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    char* p = hexa_alloc(24);\n");
        r = (r + ("    snprintf(p, 24, \" % ld));
        ", v);\n";
        r = hexa_concat(hexa_int_to_str((long)(r)), "    return p;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "}\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static const char* hexa_substr(const char* s, long a, long b) {\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    long sl = (long)strlen(s);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    if (a < 0) a = 0; if (b > sl) b = sl; if (a > b) a = b;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    long n = b - a;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    char* p = hexa_alloc(n + 1);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    memcpy(p, s + a, n); p[n] = 0;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    return p;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "}\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static long hexa_str_len(const char* s) { return (long)strlen(s); }\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static const char* hexa_read_file(const char* path) {\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    FILE* f = fopen(path, \");
        rb;
        ");\n";
        r = hexa_concat(hexa_int_to_str((long)(r)), "    if (!f) { fprintf(stderr, \");
        hexa_read_file;
        (0 % s);
        n;
        ", path); exit(1); }\n";
        r = hexa_concat(hexa_int_to_str((long)(r)), "    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    char* p = hexa_alloc(sz + 1);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    fread(p, 1, sz, f); p[sz] = 0; fclose(f);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    return p;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "}\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static long hexa_write_file(const char* path, const char* content) {\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    FILE* f = fopen(path, \");
        wb;
        ");\n";
        r = hexa_concat(hexa_int_to_str((long)(r)), "    if (!f) return 0;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    size_t n = strlen(content);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    fwrite(content, 1, n, f); fclose(f);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    return (long)n;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "}\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "typedef struct { long* d; long n; long cap; } hexa_arr;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static hexa_arr hexa_arr_new(void) { hexa_arr a = {NULL, 0, 0}; return a; }\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static hexa_arr hexa_arr_lit(const long* items, long n) {\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    hexa_arr a; a.d = (long*)malloc((n>0?n:1)*sizeof(long)); a.n = n; a.cap = (n>0?n:1);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    if (n > 0) memcpy(a.d, items, n*sizeof(long));\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    return a;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "}\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static hexa_arr hexa_arr_push(hexa_arr a, long x) {\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    if (a.n >= a.cap) {\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "        a.cap = a.cap ? a.cap * 2 : 4;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "        a.d = (long*)realloc(a.d, a.cap * sizeof(long));\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    }\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    a.d[a.n++] = x;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    return a;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "}\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static long hexa_arr_len(hexa_arr a) { return a.n; }\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static long hexa_arr_get(hexa_arr a, long i) { return a.d[i]; }\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static hexa_arr hexa_chars(const char* s) {\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    long n = (long)strlen(s);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    hexa_arr a; a.d = (long*)malloc((n>0?n:1)*sizeof(long)); a.n = n; a.cap = (n>0?n:1);\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    for (long i = 0; i < n; i++) a.d[i] = (long)(unsigned char)s[i];\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    return a;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "}\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "#include <ctype.h>\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static long hexa_is_alpha(long c) { return (long)(isalpha((int)c) ? 1 : 0); }\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static long hexa_is_alnum(long c) { return (long)(isalnum((int)c) ? 1 : 0); }\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static int hexa_main_argc = 0;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static char** hexa_main_argv = NULL;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "static hexa_arr hexa_args(void) {\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    hexa_arr a; a.n = hexa_main_argc; a.cap = hexa_main_argc;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    a.d = (long*)malloc((hexa_main_argc>0?hexa_main_argc:1)*sizeof(long));\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    for (int i = 0; i < hexa_main_argc; i++) a.d[i] = (long)hexa_main_argv[i];\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "    return a;\n");
        r = hexa_concat(hexa_int_to_str((long)(r)), "}\n\n");
        return r;
    }

long emit_program(hexa_arr stmts) {
        long out = emit_runtime();
        out = hexa_concat(out, "");
        long i = 0;
        while ((i < hexa_str_len(stmts))) {
            if ((/* unknown field kind */ 0 == "Fn")) {
                out = hexa_concat(out, hexa_int_to_str((long)(emit_fn_proto(stmts[i]))));
            }
            i = (i + 1);
        }
        out = hexa_concat(out, "\n");
        i = 0;
        while ((i < hexa_str_len(stmts))) {
            if ((/* unknown field kind */ 0 == "Fn")) {
                out = hexa_concat(out, hexa_int_to_str((long)(emit_fn_decl(stmts[i]))));
            }
            i = (i + 1);
        }
        out = hexa_concat(out, "int main(int argc, char** argv) {\n");
        out = hexa_concat(out, "    hexa_main_argc = argc; hexa_main_argv = argv;\n");
        indent_level = 1;
        i = 0;
        while ((i < hexa_str_len(stmts))) {
            if ((/* unknown field kind */ 0 != "Fn")) {
                out = hexa_concat(out, hexa_int_to_str((long)(emit_stmt(stmts[i]))));
            }
            i = (i + 1);
        }
        out = hexa_concat(out, "    return 0;\n}\n");
        return out;
    }

long cli_args() {
        return hexa_args();
    }

int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    toks = hexa_arr_push(toks, mk("EOF", ""));
    return toks;
    0;
    hexa_arr p_toks = hexa_arr_new();
    long p_pos = 0;
    hexa_arr env_names = hexa_arr_new();
    hexa_arr env_types = hexa_arr_new();
    hexa_arr env_lens = hexa_arr_new();
    hexa_arr struct_tab_names = hexa_arr_new();
    hexa_arr struct_tab_fields = hexa_arr_new();
    long indent_level = 1;
    long _argv = cli_args();
    const char* input_path = "examples/struct_min.hexa";
    const char* output_path = "build/struct_min.c";
    long _argc = hexa_str_len(_argv);
    if ((_argc >= 3)) {
        input_path = _argv[2];
    }
    if ((_argc >= 4)) {
        output_path = _argv[3];
    }
    const char* src = hexa_read_file(input_path);
    long toks = tokenize(src);
    printf("%s %ld\n", "[N1.1a] tokens:", (long)(toks.n));
    long ast = parse_program(toks);
    printf("%s %ld\n", "[N1.1a] stmts:", (long)(hexa_str_len(ast)));
    long c_src = emit_program(ast);
    hexa_write_file(output_path, c_src);
    printf("%s %s %ld %s\n", "[N1.1a] wrote", output_path, (long)(hexa_str_len(c_src)), "bytes");
    return 0;
}
