// ═══════════════════════════════════════════════════════════
//  HEXA Bootstrap Compiler — Phase 5 Step 2
//  Native C implementation of: tokenize → parse → codegen → gcc
//  This binary replaces the Rust compiler for .hexa compilation
//
//  Build: gcc -O2 bootstrap_compiler.c -o hexa_bootstrap
//  Usage: ./hexa_bootstrap input.hexa [-o output]
// ═══════════════════════════════════════════════════════════

#include "runtime.c"
#include <ctype.h>

// ══════════════════════════════════════════════════════════
// LEXER — tokenize .hexa source to token array
// ══════════════════════════════════════════════════════════

// Token: {kind, value, line, col} stored as Map
HexaVal make_token(const char* kind, const char* value, int line, int col) {
    HexaVal m = hexa_map_new();
    m = hexa_map_set(m, "kind", hexa_str(kind));
    m = hexa_map_set(m, "value", hexa_str(value));
    m = hexa_map_set(m, "line", hexa_int(line));
    m = hexa_map_set(m, "col", hexa_int(col));
    return m;
}

// 53 keywords
int is_kw(const char* w) {
    const char* kws[] = {
        "if","else","match","for","while","loop","return","yield",
        "let","mut","const","static","fn","struct","enum","trait","impl",
        "type","where","use","mod","pub","crate",
        "async","await","try","catch","throw","panic","recover",
        "own","borrow","move","drop","spawn","channel","select","atomic",
        "intent","generate","verify","optimize","scope",
        "effect","handle","resume","pure","proof","assert","invariant",
        "macro","derive","comptime","theorem","extern","break","continue",
        NULL
    };
    for (int i = 0; kws[i]; i++) {
        if (strcmp(w, kws[i]) == 0) return 1;
    }
    return 0;
}

char* capitalize(const char* w) {
    char* r = strdup(w);
    if (r[0] >= 'a' && r[0] <= 'z') r[0] -= 32;
    return r;
}

HexaVal tokenize_c(const char* src) {
    HexaVal tokens = hexa_array_new();
    int pos = 0, line = 1, col = 1;
    int len = strlen(src);

    while (pos < len) {
        char c = src[pos];

        // Skip whitespace (not newlines)
        if (c == ' ' || c == '\t' || c == '\r') { pos++; col++; continue; }

        // Newlines
        if (c == '\n') {
            tokens = hexa_array_push(tokens, make_token("Newline", "\\n", line, col));
            pos++; line++; col = 1; continue;
        }

        // Comments
        if (c == '/' && pos+1 < len && src[pos+1] == '/') {
            while (pos < len && src[pos] != '\n') pos++;
            continue;
        }

        // String literals
        if (c == '"') {
            pos++; col++;
            int start = pos;
            while (pos < len && src[pos] != '"') {
                if (src[pos] == '\\') { pos++; col++; }
                pos++; col++;
            }
            char* val = strndup(src + start, pos - start);
            tokens = hexa_array_push(tokens, make_token("StringLit", val, line, col));
            free(val);
            if (pos < len) { pos++; col++; }
            continue;
        }

        // Char literals
        if (c == '\'') {
            pos++; col++;
            char buf[2] = {src[pos], 0};
            tokens = hexa_array_push(tokens, make_token("CharLit", buf, line, col));
            pos++; col++;
            if (pos < len && src[pos] == '\'') { pos++; col++; }
            continue;
        }

        // Numbers
        if (isdigit(c)) {
            int start = pos;
            int is_float = 0;
            while (pos < len && (isdigit(src[pos]) || src[pos] == '.')) {
                if (src[pos] == '.') is_float = 1;
                pos++; col++;
            }
            // Scientific notation: 1.38e-23
            if (pos < len && (src[pos] == 'e' || src[pos] == 'E')) {
                is_float = 1;
                pos++; col++;
                if (pos < len && (src[pos] == '+' || src[pos] == '-')) { pos++; col++; }
                while (pos < len && isdigit(src[pos])) { pos++; col++; }
            }
            char* val = strndup(src + start, pos - start);
            tokens = hexa_array_push(tokens, make_token(is_float ? "FloatLit" : "IntLit", val, line, col));
            free(val);
            continue;
        }

        // Identifiers and keywords
        if (isalpha(c) || c == '_') {
            int start = pos;
            while (pos < len && (isalnum(src[pos]) || src[pos] == '_')) { pos++; col++; }
            char* word = strndup(src + start, pos - start);
            if (strcmp(word, "true") == 0 || strcmp(word, "false") == 0) {
                tokens = hexa_array_push(tokens, make_token("BoolLit", word, line, col));
            } else if (is_kw(word)) {
                char* cap = capitalize(word);
                tokens = hexa_array_push(tokens, make_token(cap, word, line, col));
                free(cap);
            } else {
                tokens = hexa_array_push(tokens, make_token("Ident", word, line, col));
            }
            free(word);
            continue;
        }

        // @ attributes
        if (c == '@') {
            pos++; col++;
            int start = pos;
            while (pos < len && (isalnum(src[pos]) || src[pos] == '_')) { pos++; col++; }
            char* attr = strndup(src + start, pos - start);
            tokens = hexa_array_push(tokens, make_token("Attribute", attr, line, col));
            free(attr);
            continue;
        }

        // Multi-char operators
        if (c == '=' && pos+1 < len && src[pos+1] == '=') { tokens = hexa_array_push(tokens, make_token("EqEq", "==", line, col)); pos += 2; col += 2; continue; }
        if (c == '=' && pos+1 < len && src[pos+1] == '>') { tokens = hexa_array_push(tokens, make_token("FatArrow", "=>", line, col)); pos += 2; col += 2; continue; }
        if (c == '+' && pos+1 < len && src[pos+1] == '=') { tokens = hexa_array_push(tokens, make_token("PlusEq", "+=", line, col)); pos += 2; col += 2; continue; }
        if (c == '-' && pos+1 < len && src[pos+1] == '=') { tokens = hexa_array_push(tokens, make_token("MinusEq", "-=", line, col)); pos += 2; col += 2; continue; }
        if (c == '*' && pos+1 < len && src[pos+1] == '=') { tokens = hexa_array_push(tokens, make_token("StarEq", "*=", line, col)); pos += 2; col += 2; continue; }
        if (c == '/' && pos+1 < len && src[pos+1] == '=') { tokens = hexa_array_push(tokens, make_token("SlashEq", "/=", line, col)); pos += 2; col += 2; continue; }
        if (c == '!' && pos+1 < len && src[pos+1] == '=') { tokens = hexa_array_push(tokens, make_token("Ne", "!=", line, col)); pos += 2; col += 2; continue; }
        if (c == '<' && pos+1 < len && src[pos+1] == '=') { tokens = hexa_array_push(tokens, make_token("Le", "<=", line, col)); pos += 2; col += 2; continue; }
        if (c == '>' && pos+1 < len && src[pos+1] == '=') { tokens = hexa_array_push(tokens, make_token("Ge", ">=", line, col)); pos += 2; col += 2; continue; }
        if (c == '&' && pos+1 < len && src[pos+1] == '&') { tokens = hexa_array_push(tokens, make_token("And", "&&", line, col)); pos += 2; col += 2; continue; }
        if (c == '|' && pos+1 < len && src[pos+1] == '|') { tokens = hexa_array_push(tokens, make_token("Or", "||", line, col)); pos += 2; col += 2; continue; }
        if (c == '-' && pos+1 < len && src[pos+1] == '>') { tokens = hexa_array_push(tokens, make_token("Arrow", "->", line, col)); pos += 2; col += 2; continue; }
        if (c == '.' && pos+1 < len && src[pos+1] == '.') {
            if (pos+2 < len && src[pos+2] == '=') {
                tokens = hexa_array_push(tokens, make_token("RangeInc", "..=", line, col)); pos += 3; col += 3;
            } else {
                tokens = hexa_array_push(tokens, make_token("Range", "..", line, col)); pos += 2; col += 2;
            }
            continue;
        }
        if (c == ':' && pos+1 < len && src[pos+1] == ':') { tokens = hexa_array_push(tokens, make_token("ColonColon", "::", line, col)); pos += 2; col += 2; continue; }
        if (c == '*' && pos+1 < len && src[pos+1] == '*') { tokens = hexa_array_push(tokens, make_token("Pow", "**", line, col)); pos += 2; col += 2; continue; }
        if (c == '#' && pos+1 < len && src[pos+1] == '{') { tokens = hexa_array_push(tokens, make_token("HashLBrace", "#{", line, col)); pos += 2; col += 2; continue; }

        // Single-char tokens
        const char* singles = "(){}[]:;,.=+-*/%<>!&|^~?";
        const char* names[] = {
            "LParen","RParen","LBrace","RBrace","LBracket","RBracket",
            "Colon","Semicolon","Comma","Dot","Eq","Plus","Minus","Star",
            "Slash","Percent","Lt","Gt","Bang","BitAnd","BitOr","Xor","Tilde","Question"
        };
        int found = 0;
        for (int i = 0; singles[i]; i++) {
            if (c == singles[i]) {
                char val[2] = {c, 0};
                tokens = hexa_array_push(tokens, make_token(names[i], val, line, col));
                pos++; col++; found = 1; break;
            }
        }
        if (found) continue;

        // Skip unknown
        pos++; col++;
    }

    tokens = hexa_array_push(tokens, make_token("Eof", "", line, col));
    return tokens;
}

// ══════════════════════════════════════════════════════════
// MAIN — read .hexa file, tokenize, report
// ══════════════════════════════════════════════════════════


// ══════════════════════════════════════════════════════════
// PARSER — recursive descent, produces Map-based AST
// ══════════════════════════════════════════════════════════

static HexaVal p_tokens;
static int p_pos = 0;

HexaVal p_peek() { return p_pos < p_tokens.arr.len ? p_tokens.arr.items[p_pos] : make_token("Eof","",0,0); }
const char* p_kind() { return hexa_map_get(p_peek(), "kind").s; }
void p_advance() { if (p_pos < p_tokens.arr.len) p_pos++; }
void p_skip_nl() { while (strcmp(p_kind(), "Newline") == 0 || strcmp(p_kind(), "Semicolon") == 0) p_advance(); }

int p_check(const char* k) { return strcmp(p_kind(), k) == 0; }
void p_expect(const char* k) {
    if (!p_check(k)) { fprintf(stderr, "Parse error: expected %s, got %s\n", k, p_kind()); }
    p_advance();
}
const char* p_expect_ident() {
    if (!p_check("Ident")) { fprintf(stderr, "Parse error: expected Ident, got %s\n", p_kind()); return ""; }
    const char* v = hexa_map_get(p_peek(), "value").s;
    p_advance();
    return v;
}

// Forward declarations
HexaVal parse_if();
HexaVal parse_expr();
HexaVal parse_stmt();
HexaVal parse_block();

HexaVal mk_node(const char* kind) {
    HexaVal m = hexa_map_new();
    m = hexa_map_set(m, "kind", hexa_str(kind));
    m = hexa_map_set(m, "name", hexa_str(""));
    m = hexa_map_set(m, "value", hexa_str(""));
    m = hexa_map_set(m, "op", hexa_str(""));
    m = hexa_map_set(m, "left", hexa_str(""));
    m = hexa_map_set(m, "right", hexa_str(""));
    m = hexa_map_set(m, "cond", hexa_str(""));
    m = hexa_map_set(m, "then_body", hexa_str(""));
    m = hexa_map_set(m, "else_body", hexa_str(""));
    m = hexa_map_set(m, "params", hexa_str(""));
    m = hexa_map_set(m, "body", hexa_str(""));
    m = hexa_map_set(m, "args", hexa_str(""));
    m = hexa_map_set(m, "items", hexa_str(""));
    m = hexa_map_set(m, "iter_expr", hexa_str(""));
    m = hexa_map_set(m, "ret_type", hexa_str(""));
    return m;
}

// ── Expressions ──────────────────────────────────────────

HexaVal parse_primary() {
    const char* k = p_kind();
    if (p_check("IntLit")) { HexaVal n = mk_node("IntLit"); n = hexa_map_set(n, "value", hexa_map_get(p_peek(), "value")); p_advance(); return n; }
    if (p_check("FloatLit")) { HexaVal n = mk_node("FloatLit"); n = hexa_map_set(n, "value", hexa_map_get(p_peek(), "value")); p_advance(); return n; }
    if (p_check("BoolLit")) { HexaVal n = mk_node("BoolLit"); n = hexa_map_set(n, "value", hexa_map_get(p_peek(), "value")); p_advance(); return n; }
    if (p_check("StringLit")) { HexaVal n = mk_node("StringLit"); n = hexa_map_set(n, "value", hexa_map_get(p_peek(), "value")); p_advance(); return n; }
    if (p_check("Ident")) {
        HexaVal n = mk_node("Ident");
        const char* id_name = hexa_map_get(p_peek(), "value").s;
        n = hexa_map_set(n, "name", hexa_str(id_name));
        p_advance();
        // StructInit: Name { field: val, ... }
        if (p_check("LBrace") && id_name[0] >= 'A' && id_name[0] <= 'Z') {
            p_advance(); // {
            HexaVal fields = hexa_array_new();
            while (!p_check("RBrace") && !p_check("Eof")) {
                p_skip_nl();
                const char* fname = p_expect_ident();
                p_expect("Colon");
                HexaVal fval = parse_expr();
                HexaVal field = mk_node("FieldInit");
                field = hexa_map_set(field, "name", hexa_str(fname));
                field = hexa_map_set(field, "left", fval);
                fields = hexa_array_push(fields, field);
                if (p_check("Comma")) p_advance();
                p_skip_nl();
            }
            p_expect("RBrace");
            HexaVal si = mk_node("StructInit");
            si = hexa_map_set(si, "name", hexa_str(id_name));
            si = hexa_map_set(si, "fields", fields);
            return si;
        }
        return n;
    }
    if (p_check("LParen")) { p_advance(); HexaVal e = parse_expr(); p_expect("RParen"); return e; }
    if (p_check("HashLBrace")) {
        // Map literal #{ "key": val, ... }
        p_advance(); p_skip_nl();
        HexaVal fields = hexa_array_new();
        while (!p_check("RBrace") && !p_check("Eof")) {
            HexaVal key = parse_expr(); // string key
            p_expect("Colon");
            HexaVal val = parse_expr();
            HexaVal entry = mk_node("MapEntry");
            entry = hexa_map_set(entry, "left", key);
            entry = hexa_map_set(entry, "right", val);
            fields = hexa_array_push(fields, entry);
            if (p_check("Comma")) p_advance();
            p_skip_nl();
        }
        p_expect("RBrace");
        HexaVal m = mk_node("MapLit");
        m = hexa_map_set(m, "items", fields);
        return m;
    }
    if (p_check("LBracket")) {
        p_advance();
        p_skip_nl();
        HexaVal items = hexa_array_new();
        while (!p_check("RBracket") && !p_check("Eof")) {
            items = hexa_array_push(items, parse_expr());
            if (p_check("Comma")) p_advance();
            p_skip_nl();
        }
        p_expect("RBracket");
        HexaVal n = mk_node("Array"); n = hexa_map_set(n, "items", items); return n;
    }
    if (p_check("If")) return parse_if();
    if (p_check("Match")) {
        // match expr { pattern => body, ... }
        p_advance();
        HexaVal scrutinee = parse_expr();
        p_expect("LBrace"); p_skip_nl();
        HexaVal arms = hexa_array_new();
        while (!p_check("RBrace") && !p_check("Eof")) {
            HexaVal pattern = parse_expr();
            if (p_check("FatArrow")) p_advance(); else if (p_check("Arrow")) p_advance(); else p_expect("FatArrow");
            HexaVal body;
            if (p_check("LBrace")) body = parse_block();
            else { body = hexa_array_new(); HexaVal es = mk_node("ExprStmt"); es = hexa_map_set(es,"left",parse_expr()); body = hexa_array_push(body, es); }
            HexaVal arm = mk_node("MatchArm");
            arm = hexa_map_set(arm, "left", pattern);
            arm = hexa_map_set(arm, "body", body);
            arms = hexa_array_push(arms, arm);
            if (p_check("Comma")) p_advance();
            p_skip_nl();
        }
        p_expect("RBrace");
        HexaVal m = mk_node("MatchExpr");
        m = hexa_map_set(m, "left", scrutinee);
        m = hexa_map_set(m, "arms", arms);
        return m;
    }
    if (p_check("BitOr")) {
        // Lambda: |params| expr
        p_advance();
        HexaVal params = hexa_array_new();
        while (!p_check("BitOr") && !p_check("Eof")) {
            const char* pn = p_expect_ident();
            params = hexa_array_push(params, hexa_str(pn));
            if (p_check("Comma")) p_advance();
        }
        p_expect("BitOr");
        HexaVal body = p_check("LBrace") ? parse_block() : parse_expr();
        HexaVal n = mk_node("Lambda");
        n = hexa_map_set(n, "params", params);
        n = hexa_map_set(n, "left", body);
        return n;
    }
    if (p_check("Minus")) {
        p_advance();
        HexaVal n = mk_node("UnaryOp");
        n = hexa_map_set(n, "op", hexa_str("-"));
        n = hexa_map_set(n, "left", parse_primary());
        return n;
    }
    if (p_check("Bang")) {
        p_advance();
        HexaVal n = mk_node("UnaryOp");
        n = hexa_map_set(n, "op", hexa_str("!"));
        n = hexa_map_set(n, "left", parse_primary());
        return n;
    }
    fprintf(stderr, "Unexpected token: %s '%s'\n", p_kind(), hexa_map_get(p_peek(), "value").s);
    p_advance();
    return mk_node("Void");
}

HexaVal parse_postfix() {
    HexaVal expr = parse_primary();
    for (;;) {
        if (p_check("LParen")) {
            p_advance();
            HexaVal args = hexa_array_new();
            while (!p_check("RParen") && !p_check("Eof")) {
                args = hexa_array_push(args, parse_expr());
                if (p_check("Comma")) p_advance();
            }
            p_expect("RParen");
            HexaVal call = mk_node("Call");
            call = hexa_map_set(call, "left", expr);
            call = hexa_map_set(call, "args", args);
            expr = call;
        } else if (p_check("Dot")) {
            p_advance();
            const char* field = p_expect_ident();
            HexaVal f = mk_node("Field");
            f = hexa_map_set(f, "left", expr);
            f = hexa_map_set(f, "name", hexa_str(field));
            expr = f;
        } else if (p_check("LBracket")) {
            p_advance();
            HexaVal idx = parse_expr();
            p_expect("RBracket");
            HexaVal ix = mk_node("Index");
            ix = hexa_map_set(ix, "left", expr);
            ix = hexa_map_set(ix, "right", idx);
            expr = ix;
        } else break;
    }
    return expr;
}

int prec(const char* op) {
    if (strcmp(op,"Or")==0) return 1;
    if (strcmp(op,"And")==0) return 2;
    if (strcmp(op,"EqEq")==0||strcmp(op,"Ne")==0) return 3;
    if (strcmp(op,"Lt")==0||strcmp(op,"Gt")==0||strcmp(op,"Le")==0||strcmp(op,"Ge")==0) return 4;
    if (strcmp(op,"Plus")==0||strcmp(op,"Minus")==0) return 5;
    if (strcmp(op,"Star")==0||strcmp(op,"Slash")==0||strcmp(op,"Percent")==0) return 6;
    if (strcmp(op,"Range")==0) return 0;
    return -1;
}

const char* op_str(const char* k) {
    if (strcmp(k,"Plus")==0) return "+"; if (strcmp(k,"Minus")==0) return "-";
    if (strcmp(k,"Star")==0) return "*"; if (strcmp(k,"Slash")==0) return "/";
    if (strcmp(k,"Percent")==0) return "%";
    if (strcmp(k,"EqEq")==0) return "=="; if (strcmp(k,"Ne")==0) return "!=";
    if (strcmp(k,"Lt")==0) return "<"; if (strcmp(k,"Gt")==0) return ">";
    if (strcmp(k,"Le")==0) return "<="; if (strcmp(k,"Ge")==0) return ">=";
    if (strcmp(k,"And")==0) return "&&"; if (strcmp(k,"Or")==0) return "||";
    if (strcmp(k,"Range")==0) return "..";
    return k;
}

HexaVal parse_binop(int min_prec) {
    HexaVal left = parse_postfix();
    for (;;) {
        int p = prec(p_kind());
        if (p < min_prec) break;
        const char* ok = p_kind();
        int is_range = strcmp(ok, "Range") == 0;
        const char* os = op_str(ok);
        p_advance();
        HexaVal right = parse_binop(p + 1);
        HexaVal node = mk_node(is_range ? "Range" : "BinOp");
        node = hexa_map_set(node, "op", hexa_str(os));
        node = hexa_map_set(node, "left", left);
        node = hexa_map_set(node, "right", right);
        left = node;
    }
    return left;
}

HexaVal parse_expr() { return parse_binop(0); }

// ── If expression ────────────────────────────────────────

HexaVal parse_if() {
    p_advance(); // consume 'if'
    HexaVal cond = parse_expr();
    HexaVal then_b = parse_block();
    HexaVal else_b = hexa_str("");
    p_skip_nl();
    fprintf(stderr, "[parse_if] after then block, next token: %s '%s'\n", p_kind(), hexa_map_get(p_peek(),"value").s);
    if (p_check("Else")) {
        p_advance();
        if (p_check("If")) { else_b = hexa_array_new(); else_b = hexa_array_push(else_b, parse_if()); }
        else else_b = parse_block();
    }
    HexaVal n = mk_node("IfExpr");
    n = hexa_map_set(n, "cond", cond);
    n = hexa_map_set(n, "then_body", then_b);
    n = hexa_map_set(n, "else_body", else_b);
    return n;
}

// ── Block ────────────────────────────────────────────────

HexaVal parse_block() {
    p_expect("LBrace");
    p_skip_nl();
    HexaVal stmts = hexa_array_new();
    while (!p_check("RBrace") && !p_check("Eof")) {
        stmts = hexa_array_push(stmts, parse_stmt());
        p_skip_nl();
    }
    p_expect("RBrace");
    return stmts;
}

// ── Statements ───────────────────────────────────────────

HexaVal parse_params() {
    HexaVal params = hexa_array_new();
    if (p_check("RParen")) return params;
    HexaVal pm = mk_node("Param");
    pm = hexa_map_set(pm, "name", hexa_str(p_expect_ident()));
    if (p_check("Colon")) { p_advance(); p_expect_ident(); } // skip type annotation
    params = hexa_array_push(params, pm);
    while (p_check("Comma")) {
        p_advance();
        HexaVal pm2 = mk_node("Param");
        pm2 = hexa_map_set(pm2, "name", hexa_str(p_expect_ident()));
        if (p_check("Colon")) { p_advance(); p_expect_ident(); }
        params = hexa_array_push(params, pm2);
    }
    return params;
}

HexaVal parse_stmt() {
    p_skip_nl();
    const char* k = p_kind();

    if (p_check("Let")) {
        p_advance();
        int is_mut = 0;
        if (p_check("Mut")) { p_advance(); is_mut = 1; }
        const char* name = p_expect_ident();
        if (p_check("Colon")) { p_advance(); p_expect_ident(); }
        HexaVal init = hexa_str("");
        if (p_check("Eq")) { p_advance(); init = parse_expr(); }
        HexaVal n = mk_node(is_mut ? "LetMutStmt" : "LetStmt");
        n = hexa_map_set(n, "name", hexa_str(name));
        n = hexa_map_set(n, "left", init);
        return n;
    }
    if (p_check("Const")) {
        p_advance();
        const char* name = p_expect_ident();
        if (p_check("Colon")) { p_advance(); p_expect_ident(); }
        p_expect("Eq");
        HexaVal n = mk_node("ConstStmt");
        n = hexa_map_set(n, "name", hexa_str(name));
        n = hexa_map_set(n, "left", parse_expr());
        return n;
    }
    if (p_check("Return")) {
        p_advance();
        HexaVal n = mk_node("ReturnStmt");
        if (!p_check("Newline") && !p_check("RBrace") && !p_check("Eof"))
            n = hexa_map_set(n, "left", parse_expr());
        return n;
    }
    if (p_check("Fn")) {
        p_advance();
        const char* name = p_expect_ident();
        p_expect("LParen");
        HexaVal params = parse_params();
        p_expect("RParen");
        HexaVal ret = hexa_str("");
        if (p_check("Arrow")) { p_advance(); ret = hexa_str(p_expect_ident()); }
        HexaVal body = parse_block();
        HexaVal n = mk_node("FnDecl");
        n = hexa_map_set(n, "name", hexa_str(name));
        n = hexa_map_set(n, "params", params);
        n = hexa_map_set(n, "body", body);
        n = hexa_map_set(n, "ret_type", ret);
        return n;
    }
    if (p_check("While")) {
        p_advance();
        HexaVal cond = parse_expr();
        HexaVal body = parse_block();
        HexaVal n = mk_node("WhileStmt");
        n = hexa_map_set(n, "cond", cond);
        n = hexa_map_set(n, "body", body);
        return n;
    }
    if (p_check("For")) {
        p_advance();
        const char* var = p_expect_ident();
        p_expect("In");
        HexaVal iter = parse_expr();
        HexaVal body = parse_block();
        HexaVal n = mk_node("ForStmt");
        n = hexa_map_set(n, "name", hexa_str(var));
        n = hexa_map_set(n, "iter_expr", iter);
        n = hexa_map_set(n, "body", body);
        return n;
    }
    if (p_check("If")) {
        HexaVal n = mk_node("ExprStmt");
        n = hexa_map_set(n, "left", parse_if());
        return n;
    }
    if (p_check("Use")) {
        p_advance();
        const char* mod = p_expect_ident();
        while (p_check("ColonColon")) { p_advance(); mod = p_expect_ident(); }
        HexaVal n = mk_node("UseStmt");
        n = hexa_map_set(n, "name", hexa_str(mod));
        return n;
    }
    if (p_check("Struct")) {
        p_advance();
        const char* name = p_expect_ident();
        p_expect("LBrace"); p_skip_nl();
        while (!p_check("RBrace") && !p_check("Eof")) { p_advance(); } // skip fields
        p_expect("RBrace");
        HexaVal n = mk_node("StructDecl");
        n = hexa_map_set(n, "name", hexa_str(name));
        return n;
    }
    if (p_check("Try")) {
        p_advance();
        HexaVal try_body = parse_block();
        p_skip_nl();
        HexaVal catch_var = hexa_str("_e");
        HexaVal catch_body = hexa_str("");
        if (p_check("Catch")) {
            p_advance();
            if (p_check("LParen")) { p_advance(); catch_var = hexa_str(p_expect_ident()); p_expect("RParen"); }
            catch_body = parse_block();
        }
        HexaVal n = mk_node("TryCatch");
        n = hexa_map_set(n, "name", catch_var);
        n = hexa_map_set(n, "left", try_body);
        n = hexa_map_set(n, "right", catch_body);
        return n;
    }
    if (p_check("Throw")) {
        p_advance();
        HexaVal n = mk_node("ThrowStmt");
        n = hexa_map_set(n, "left", parse_expr());
        return n;
    }
    if (p_check("Break")) { p_advance(); return mk_node("BreakStmt"); }
    if (p_check("Continue")) { p_advance(); return mk_node("ContinueStmt"); }

    // Expression or assignment
    HexaVal expr = parse_expr();
    // Compound assignment: +=, -=, *=, /=
    const char* compound_ops[] = {"PlusEq", "MinusEq", "StarEq", "SlashEq"};
    const char* op_chars[] = {"+", "-", "*", "/"};
    for (int _ci = 0; _ci < 4; _ci++) {
        if (p_check(compound_ops[_ci])) {
            p_advance();
            HexaVal rhs = parse_expr();
            HexaVal binop = mk_node("BinOp");
            binop = hexa_map_set(binop, "op", hexa_str(op_chars[_ci]));
            binop = hexa_map_set(binop, "left", expr);
            binop = hexa_map_set(binop, "right", rhs);
            HexaVal n = mk_node("AssignStmt");
            n = hexa_map_set(n, "left", expr);
            n = hexa_map_set(n, "right", binop);
            return n;
        }
    }
    if (p_check("Eq")) {
        p_advance();
        HexaVal rhs = parse_expr();
        HexaVal n = mk_node("AssignStmt");
        n = hexa_map_set(n, "left", expr);
        n = hexa_map_set(n, "right", rhs);
        return n;
    }
    HexaVal n = mk_node("ExprStmt");
    n = hexa_map_set(n, "left", expr);
    return n;
}

HexaVal parse_c(HexaVal tokens) {
    p_tokens = tokens;
    p_pos = 0;
    HexaVal stmts = hexa_array_new();
    p_skip_nl();
    while (!p_check("Eof")) {
        stmts = hexa_array_push(stmts, parse_stmt());
        p_skip_nl();
    }
    return stmts;
}

// ══════════════════════════════════════════════════════════
// CODEGEN — AST → C source code (using runtime.c HexaVal)
// ══════════════════════════════════════════════════════════

void gen_indent(char* buf, int d) { for (int i=0;i<d;i++) strcat(buf, "    "); }

void gen_expr(HexaVal node, char* buf) {
    const char* k = hexa_map_get(node, "kind").s;
    if (strcmp(k,"IntLit")==0) { strcat(buf, "hexa_int("); strcat(buf, hexa_map_get(node,"value").s); strcat(buf, ")"); return; }
    if (strcmp(k,"FloatLit")==0) { strcat(buf, "hexa_float("); strcat(buf, hexa_map_get(node,"value").s); strcat(buf, ")"); return; }
    if (strcmp(k,"BoolLit")==0) { strcat(buf, strcmp(hexa_map_get(node,"value").s,"true")==0 ? "hexa_bool(1)" : "hexa_bool(0)"); return; }
    if (strcmp(k,"StringLit")==0) { strcat(buf, "hexa_str(\""); strcat(buf, hexa_map_get(node,"value").s); strcat(buf, "\")"); return; }
    if (strcmp(k,"Ident")==0) { strcat(buf, hexa_map_get(node,"name").s); return; }
    if (strcmp(k,"BinOp")==0) {
        const char* op = hexa_map_get(node,"op").s;
        if (strcmp(op,"+")==0) { strcat(buf,"hexa_add("); gen_expr(hexa_map_get(node,"left"),buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"right"),buf); strcat(buf,")"); return; }
        if (strcmp(op,"==")==0) { strcat(buf,"hexa_eq("); gen_expr(hexa_map_get(node,"left"),buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"right"),buf); strcat(buf,")"); return; }
        if (strcmp(op,"-")==0) { strcat(buf,"hexa_sub("); gen_expr(hexa_map_get(node,"left"),buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"right"),buf); strcat(buf,")"); return; }
        if (strcmp(op,"*")==0) { strcat(buf,"hexa_mul("); gen_expr(hexa_map_get(node,"left"),buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"right"),buf); strcat(buf,")"); return; }
        if (strcmp(op,"/")==0) { strcat(buf,"hexa_div("); gen_expr(hexa_map_get(node,"left"),buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"right"),buf); strcat(buf,")"); return; }
        if (strcmp(op,"%")==0) { strcat(buf,"hexa_mod("); gen_expr(hexa_map_get(node,"left"),buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"right"),buf); strcat(buf,")"); return; }
        strcat(buf,"hexa_int(("); gen_expr(hexa_map_get(node,"left"),buf); strcat(buf,").i "); strcat(buf,op); strcat(buf," ("); gen_expr(hexa_map_get(node,"right"),buf); strcat(buf,").i)");
        return;
    }
    if (strcmp(k,"UnaryOp")==0) {
        const char* op = hexa_map_get(node,"op").s;
        if (strcmp(op,"-")==0) { strcat(buf,"hexa_sub(hexa_int(0), "); gen_expr(hexa_map_get(node,"left"),buf); strcat(buf,")"); return; }
        if (strcmp(op,"!")==0) { strcat(buf,"hexa_bool(!hexa_truthy("); gen_expr(hexa_map_get(node,"left"),buf); strcat(buf,"))"); return; }
    }
    if (strcmp(k,"Call")==0) {
        HexaVal callee = hexa_map_get(node,"left");
        const char* ck = hexa_map_get(callee,"kind").s;
        if (strcmp(ck,"Ident")==0) {
            const char* fn = hexa_map_get(callee,"name").s;
            if (strcmp(fn,"println")==0) {
                HexaVal args=hexa_map_get(node,"args");
                if(args.tag==TAG_ARRAY&&args.arr.len>1) {
                    // Multi-arg: use hexa_println_multi helper
                    strcat(buf,"(");
                    for(int _pi=0;_pi<args.arr.len;_pi++) {
                        if(_pi>0) strcat(buf,", printf(\" \"), ");
                        strcat(buf,"hexa_print_val("); gen_expr(args.arr.items[_pi],buf); strcat(buf,")");
                    }
                    strcat(buf,", printf(\"\\n\"), hexa_void())");
                } else if(args.tag==TAG_ARRAY&&args.arr.len==1) {
                    strcat(buf,"(hexa_println("); gen_expr(args.arr.items[0],buf); strcat(buf,"), hexa_void())");
                } else {
                    strcat(buf,"(printf(\"\\n\"), hexa_void())");
                }
                return;
            }
            if (strcmp(fn,"len")==0) { strcat(buf,"hexa_int(hexa_len("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,"))"); return; }
            if (strcmp(fn,"to_string")==0) { strcat(buf,"hexa_to_string("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,")"); return; }
            if (strcmp(fn,"sqrt")==0) { strcat(buf,"hexa_sqrt("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,")"); return; }
            if (strcmp(fn,"pow")==0) { strcat(buf,"hexa_pow("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"args").arr.items[1],buf); strcat(buf,")"); return; }
            if (strcmp(fn,"floor")==0) { strcat(buf,"hexa_floor("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,")"); return; }
            if (strcmp(fn,"ceil")==0) { strcat(buf,"hexa_ceil("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,")"); return; }
            if (strcmp(fn,"abs")==0) { strcat(buf,"hexa_abs("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,")"); return; }
            if (strcmp(fn,"ln")==0||strcmp(fn,"log")==0) { strcat(buf,"hexa_float(log("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".tag==TAG_FLOAT?"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".f:(double)"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".i))"); return; }
            if (strcmp(fn,"log10")==0) { strcat(buf,"hexa_float(log10("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".tag==TAG_FLOAT?"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".f:(double)"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".i))"); return; }
            if (strcmp(fn,"exp")==0) { strcat(buf,"hexa_float(exp("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".tag==TAG_FLOAT?"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".f:(double)"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".i))"); return; }
            if (strcmp(fn,"sin")==0) { strcat(buf,"hexa_float(sin("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".tag==TAG_FLOAT?"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".f:(double)"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".i))"); return; }
            if (strcmp(fn,"cos")==0) { strcat(buf,"hexa_float(cos("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".tag==TAG_FLOAT?"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".f:(double)"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".i))"); return; }
            if (strcmp(fn,"to_float")==0) { strcat(buf,"hexa_float("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".tag==TAG_FLOAT?"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".f:(double)"); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,".i)"); return; }
            if (strcmp(fn,"format_float")==0) { strcat(buf,"hexa_format_float("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"args").arr.items[1],buf); strcat(buf,")"); return; }
            if (strcmp(fn,"args")==0) { strcat(buf,"hexa_args()"); return; }
            if (strcmp(fn,"pad_left")==0) { strcat(buf,"hexa_pad_left("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"args").arr.items[1],buf); strcat(buf,")"); return; }
            if (strcmp(fn,"pad_right")==0) { strcat(buf,"hexa_pad_right("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"args").arr.items[1],buf); strcat(buf,")"); return; }
            if (strcmp(fn,"format")==0) {
                HexaVal fargs = hexa_map_get(node,"args");
                if (fargs.tag==TAG_ARRAY && fargs.arr.len == 2) {
                    strcat(buf,"hexa_format("); gen_expr(fargs.arr.items[0],buf); strcat(buf,", "); gen_expr(fargs.arr.items[1],buf); strcat(buf,")");
                } else if (fargs.tag==TAG_ARRAY && fargs.arr.len > 2) {
                    // Multi-arg: format(fmt, a, b, c) → hexa_format_n(fmt, [a,b,c])
                    strcat(buf,"hexa_format_n("); gen_expr(fargs.arr.items[0],buf);
                    strcat(buf,", ");
                    // Build args array
                    for (int _fi=1; _fi<fargs.arr.len; _fi++) strcat(buf,"hexa_array_push(");
                    strcat(buf,"hexa_array_new()");
                    for (int _fi=1; _fi<fargs.arr.len; _fi++) {
                        strcat(buf,", "); gen_expr(fargs.arr.items[_fi],buf); strcat(buf,")");
                    }
                    strcat(buf,")");
                } else {
                    strcat(buf,"hexa_to_string("); gen_expr(fargs.arr.items[0],buf); strcat(buf,")");
                }
                return;
            }
            // User function
            strcat(buf,fn); strcat(buf,"(");
            HexaVal args = hexa_map_get(node,"args");
            for (int i=0; args.tag==TAG_ARRAY && i<args.arr.len; i++) { if(i>0) strcat(buf,", "); gen_expr(args.arr.items[i],buf); }
            strcat(buf,")");
            return;
        }
        if (strcmp(ck,"Field")==0) {
            const char* method = hexa_map_get(callee,"name").s;
            if (strcmp(method,"len")==0) { strcat(buf,"hexa_int(hexa_len("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,"))"); return; }
            if (strcmp(method,"chars")==0) { strcat(buf,"hexa_str_chars("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,")"); return; }
            if (strcmp(method,"contains")==0) { strcat(buf,"(("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,").tag==TAG_ARRAY?hexa_bool(hexa_array_contains("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,","); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,")):hexa_bool(hexa_str_contains("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,","); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,")))"); return; }
            if (strcmp(method,"split")==0) { strcat(buf,"hexa_str_split("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,")"); return; }
            if (strcmp(method,"trim")==0) { strcat(buf,"hexa_str_trim("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,")"); return; }
            if (strcmp(method,"replace")==0) { strcat(buf,"hexa_str_replace("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"args").arr.items[1],buf); strcat(buf,")"); return; }
            if (strcmp(method,"to_upper")==0) { strcat(buf,"hexa_str_to_upper("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,")"); return; }
            if (strcmp(method,"to_lower")==0) { strcat(buf,"hexa_str_to_lower("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,")"); return; }
            if (strcmp(method,"starts_with")==0) { strcat(buf,"hexa_bool(strncmp(("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,").s, ("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,").s, strlen(("); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,").s))==0)"); return; }
            if (strcmp(method,"map")==0) { strcat(buf,"/* .map() not in C codegen */hexa_array_new()"); return; }
            if (strcmp(method,"filter")==0) { strcat(buf,"/* .filter() not in C codegen */hexa_array_new()"); return; }
            if (strcmp(method,"join")==0) { strcat(buf,"hexa_str_join("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,")"); return; }
            if (strcmp(method,"repeat")==0) { strcat(buf,"hexa_str_repeat("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,")"); return; }
            if (strcmp(method,"push")==0) { strcat(buf,"hexa_array_push("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,", "); gen_expr(hexa_map_get(node,"args").arr.items[0],buf); strcat(buf,")"); return; }
            if (strcmp(method,"len")==0) { strcat(buf,"hexa_int(hexa_len("); gen_expr(hexa_map_get(callee,"left"),buf); strcat(buf,"))"); return; }
        }
    }
    if (strcmp(k,"StructInit")==0) {
        HexaVal fields = hexa_map_get(node, "fields");
        int nf = fields.tag==TAG_ARRAY ? fields.arr.len : 0;
        // Build nested hexa_map_set calls
        for (int i=0; i<nf; i++) strcat(buf, "hexa_map_set(");
        strcat(buf, "hexa_map_new()");
        for (int i=0; i<nf; i++) {
            HexaVal f = fields.arr.items[i];
            strcat(buf, ", \""); strcat(buf, hexa_map_get(f,"name").s); strcat(buf, "\", ");
            gen_expr(hexa_map_get(f,"left"), buf);
            strcat(buf, ")");
        }
        return;
    }
    if (strcmp(k,"MapLit")==0) {
        HexaVal items = hexa_map_get(node,"items");
        int n = items.tag==TAG_ARRAY ? items.arr.len : 0;
        for (int i=0; i<n; i++) strcat(buf, "hexa_map_set(");
        strcat(buf, "hexa_map_new()");
        for (int i=0; i<n; i++) {
            HexaVal entry = items.arr.items[i];
            strcat(buf, ", ("); gen_expr(hexa_map_get(entry,"left"),buf); strcat(buf, ").s, ");
            gen_expr(hexa_map_get(entry,"right"),buf);
            strcat(buf, ")");
        }
        return;
    }
    if (strcmp(k,"Array")==0) {
        HexaVal items = hexa_map_get(node,"items");
        int n = items.tag==TAG_ARRAY ? items.arr.len : 0;
        if (n == 0) { strcat(buf,"hexa_array_new()"); return; }
        // Build nested hexa_array_push calls
        // hexa_array_push(hexa_array_push(hexa_array_new(), item0), item1)
        for (int i=0; i<n; i++) strcat(buf, "hexa_array_push(");
        strcat(buf, "hexa_array_new()");
        for (int i=0; i<n; i++) {
            strcat(buf, ", ");
            gen_expr(items.arr.items[i], buf);
            strcat(buf, ")");
        }
        return;
    }
    if (strcmp(k,"Field")==0) {
        strcat(buf, "hexa_map_get(");
        gen_expr(hexa_map_get(node,"left"), buf);
        strcat(buf, ", \""); strcat(buf, hexa_map_get(node,"name").s); strcat(buf, "\")");
        return;
    }
    if (strcmp(k,"MatchExpr")==0) {
        // match as nested ternary: eq(_m,p1)?v1 : eq(_m,p2)?v2 : default
        strcat(buf, "({HexaVal _m="); gen_expr(hexa_map_get(node,"left"),buf); strcat(buf,"; ");
        HexaVal arms = hexa_map_get(node,"arms");
        int n = arms.tag==TAG_ARRAY ? arms.arr.len : 0;
        for (int i=0; i<n; i++) {
            HexaVal arm = arms.arr.items[i];
            HexaVal pat = hexa_map_get(arm,"left");
            HexaVal body = hexa_map_get(arm,"body");
            const char* pk = hexa_map_get(pat,"kind").s;
            int is_wildcard = (strcmp(pk,"Ident")==0 && strcmp(hexa_map_get(pat,"name").s,"_")==0);
            if (is_wildcard) {
                // Default arm — just emit the value
                if (body.tag==TAG_ARRAY && body.arr.len>0) gen_expr(hexa_map_get(body.arr.items[0],"left"),buf);
                else strcat(buf, "hexa_void()");
            } else {
                strcat(buf, "hexa_truthy(hexa_eq(_m,");
                gen_expr(pat, buf);
                strcat(buf, ")) ? ");
                if (body.tag==TAG_ARRAY && body.arr.len>0) gen_expr(hexa_map_get(body.arr.items[0],"left"),buf);
                else strcat(buf, "hexa_void()");
                strcat(buf, " : ");
            }
        }
        if (n == 0 || (n > 0 && strcmp(hexa_map_get(hexa_map_get(arms.arr.items[n-1],"left"),"kind").s,"Ident")!=0))
            strcat(buf, "hexa_void()");
        strcat(buf, ";})");
        return;
    }
    if (strcmp(k,"Index")==0) {
        strcat(buf,"hexa_array_get("); gen_expr(hexa_map_get(node,"left"),buf); strcat(buf,",("); gen_expr(hexa_map_get(node,"right"),buf); strcat(buf,").i)"); return;
    }
    if (strcmp(k,"IfExpr")==0) {
        strcat(buf,"(hexa_truthy("); gen_expr(hexa_map_get(node,"cond"),buf); strcat(buf,") ? ");
        HexaVal tb = hexa_map_get(node,"then_body");
        if (tb.tag==TAG_ARRAY && tb.arr.len>0) {
            HexaVal tl = tb.arr.items[tb.arr.len-1];
            const char* tlk = hexa_map_get(tl,"kind").s;
            if (strcmp(tlk,"ExprStmt")==0) gen_expr(hexa_map_get(tl,"left"),buf);
            else if (strcmp(tlk,"ReturnStmt")==0) gen_expr(hexa_map_get(tl,"left"),buf);
            else gen_expr(tl,buf);
        } else strcat(buf,"hexa_void()");
        strcat(buf," : ");
        HexaVal eb = hexa_map_get(node,"else_body");
        if (eb.tag==TAG_ARRAY && eb.arr.len>0) {
            HexaVal el = eb.arr.items[eb.arr.len-1];
            const char* elk = hexa_map_get(el,"kind").s;
            if (strcmp(elk,"ExprStmt")==0) gen_expr(hexa_map_get(el,"left"),buf);
            else if (strcmp(elk,"ReturnStmt")==0) gen_expr(hexa_map_get(el,"left"),buf);
            else gen_expr(el,buf);
        } else strcat(buf,"hexa_void()");
        strcat(buf,")");
        return;
    }
    if (strcmp(k,"Range")==0) {
        // Range used in for loops, shouldn't appear as standalone expr
        strcat(buf,"/* range */");
        return;
    }
    strcat(buf, "/* "); strcat(buf, k); strcat(buf, " */");
}

void gen_stmt(HexaVal node, int depth, char* buf) {
    const char* k = hexa_map_get(node, "kind").s;
    gen_indent(buf, depth);
    char pad[64]; pad[0]=0; for(int _p=0;_p<depth;_p++) strcat(pad,"    ");

    if (strcmp(k,"LetStmt")==0||strcmp(k,"LetMutStmt")==0||strcmp(k,"ConstStmt")==0) {
        // Generate init expr into separate buffer to avoid corruption
        char* init_buf = calloc(1, 65536);
        HexaVal init = hexa_map_get(node,"left");
        if (init.tag == TAG_STR && strlen(init.s)==0) strcpy(init_buf,"hexa_void()");
        else gen_expr(init, init_buf);
        strcat(buf, "HexaVal "); strcat(buf, hexa_map_get(node,"name").s);
        strcat(buf, " = "); strcat(buf, init_buf); strcat(buf, ";\n"); free(init_buf); return;
    }
    if (strcmp(k,"AssignStmt")==0) {
        gen_expr(hexa_map_get(node,"left"), buf); strcat(buf, " = "); gen_expr(hexa_map_get(node,"right"), buf); strcat(buf, ";\n"); return;
    }
    if (strcmp(k,"ReturnStmt")==0) {
        strcat(buf, "return ");
        HexaVal ret = hexa_map_get(node,"left");
        if (ret.tag == TAG_STR && strlen(ret.s)==0) strcat(buf,"hexa_void()");
        else gen_expr(ret, buf);
        strcat(buf, ";\n"); return;
    }
    if (strcmp(k,"ExprStmt")==0) {
        HexaVal expr = hexa_map_get(node,"left");
        if (expr.tag == TAG_STR && strlen(expr.s)==0) return;
        const char* ek = hexa_map_get(expr,"kind").s;
        // Multi-arg println → separate print statements
        if (strcmp(ek,"Call")==0) {
            HexaVal cl = hexa_map_get(expr,"left");
            if (cl.tag!=TAG_STR && strcmp(hexa_map_get(cl,"kind").s,"Ident")==0 &&
                strcmp(hexa_map_get(cl,"name").s,"println")==0) {
                HexaVal pa = hexa_map_get(expr,"args");
                if (pa.tag==TAG_ARRAY) {
                    for (int _i=0;_i<pa.arr.len;_i++) {
                        if(_i>0) { gen_indent(buf,depth); strcat(buf,"printf(\" \");\n"); }
                        gen_indent(buf,depth);
                        strcat(buf,"hexa_print_val("); gen_expr(pa.arr.items[_i],buf); strcat(buf,");\n");
                    }
                    gen_indent(buf,depth); strcat(buf,"printf(\"\\n\");\n");
                    return;
                }
            }
        }
        if (strcmp(ek,"IfExpr")==0) {
            strcat(buf, "if (hexa_truthy("); gen_expr(hexa_map_get(expr,"cond"), buf); strcat(buf, ")) {\n");
            HexaVal tb = hexa_map_get(expr,"then_body");
            for (int i=0; tb.tag==TAG_ARRAY && i<tb.arr.len; i++) gen_stmt(tb.arr.items[i], depth+1, buf);
            gen_indent(buf,depth); strcat(buf,"}");
            HexaVal eb = hexa_map_get(expr,"else_body");
            if (eb.tag==TAG_ARRAY && eb.arr.len>0) {
                // Check for else-if chain
                // Check if first element is IfExpr (direct or wrapped in ExprStmt)
                HexaVal first_eb = eb.arr.items[0];
                const char* ebk = hexa_map_get(first_eb,"kind").s;
                if (eb.arr.len==1 && (strcmp(ebk,"IfExpr")==0 || strcmp(ebk,"ExprStmt")==0)) {
                    HexaVal inner = strcmp(ebk,"IfExpr")==0 ? first_eb : hexa_map_get(first_eb,"left");
                    if (inner.tag!=TAG_STR && strcmp(hexa_map_get(inner,"kind").s,"IfExpr")==0) {
                        strcat(buf," else ");
                        // Recurse: generate the inner if as if it were a top-level if statement
                        HexaVal wrapper = mk_node("ExprStmt");
                        wrapper = hexa_map_set(wrapper, "left", inner);
                        gen_stmt(wrapper, depth, buf);
                        goto done_if;
                    }
                }
                strcat(buf," else {\n");
                for (int i=0; i<eb.arr.len; i++) gen_stmt(eb.arr.items[i], depth+1, buf);
                gen_indent(buf,depth); strcat(buf,"}");
            }
            done_if:
            strcat(buf,"\n"); return;
        }
        gen_expr(expr, buf); strcat(buf, ";\n"); return;
    }
    if (strcmp(k,"WhileStmt")==0) {
        strcat(buf, "while (hexa_truthy("); gen_expr(hexa_map_get(node,"cond"), buf); strcat(buf, ")) {\n");
        HexaVal body = hexa_map_get(node,"body");
        for (int i=0; body.tag==TAG_ARRAY && i<body.arr.len; i++) gen_stmt(body.arr.items[i], depth+1, buf);
        gen_indent(buf,depth); strcat(buf, "}\n"); return;
    }
    if (strcmp(k,"ForStmt")==0) {
        HexaVal iter = hexa_map_get(node,"iter_expr");
        if (strcmp(hexa_map_get(iter,"kind").s,"Range")==0) {
            strcat(buf,"for (HexaVal "); strcat(buf,hexa_map_get(node,"name").s); strcat(buf," = ");
            gen_expr(hexa_map_get(iter,"left"),buf); strcat(buf,"; ");
            strcat(buf,hexa_map_get(node,"name").s); strcat(buf,".i < (");
            gen_expr(hexa_map_get(iter,"right"),buf); strcat(buf,").i; ");
            strcat(buf,hexa_map_get(node,"name").s); strcat(buf,".i++) {\n");
        } else {
            strcat(buf,"{ HexaVal _arr="); gen_expr(iter,buf); strcat(buf,";\nfor(int _i=0;_i<_arr.arr.len;_i++){HexaVal ");
            strcat(buf,hexa_map_get(node,"name").s); strcat(buf,"=_arr.arr.items[_i];\n");
        }
        HexaVal body = hexa_map_get(node,"body");
        for (int i=0; body.tag==TAG_ARRAY && i<body.arr.len; i++) gen_stmt(body.arr.items[i], depth+1, buf);
        gen_indent(buf,depth); strcat(buf, "}");
        if (strcmp(hexa_map_get(iter,"kind").s,"Range")!=0) strcat(buf,"}");
        strcat(buf,"\n"); return;
    }
    if (strcmp(k,"TryCatch")==0) {
        // Simplified: just execute try body, ignore catch (no real exceptions in C)
        strcat(buf, "/* try */ {\n");
        HexaVal tb = hexa_map_get(node,"left");
        for (int i=0; tb.tag==TAG_ARRAY && i<tb.arr.len; i++) gen_stmt(tb.arr.items[i], depth+1, buf);
        gen_indent(buf,depth); strcat(buf, "}\n");
        return;
    }
    if (strcmp(k,"ThrowStmt")==0) {
        strcat(buf, "/* throw */ fprintf(stderr, \"error\\n\"); exit(1);\n");
        return;
    }
    if (strcmp(k,"UseStmt")==0) {
        strcat(buf, "/* use "); strcat(buf, hexa_map_get(node,"name").s); strcat(buf, " */\n");
        return;
    }
    if (strcmp(k,"BreakStmt")==0) { strcat(buf, "break;\n"); return; }
    if (strcmp(k,"ContinueStmt")==0) { strcat(buf, "continue;\n"); return; }
    if (strcmp(k,"StructDecl")==0) { strcat(buf,"/* struct "); strcat(buf,hexa_map_get(node,"name").s); strcat(buf," */\n"); return; }
    strcat(buf, "/* "); strcat(buf, k); strcat(buf, " */\n");
}

char* codegen_c_full(HexaVal ast) {
    char* buf = calloc(1, 1024*1024); // 1MB buffer
    strcat(buf, "// Generated by HEXA Bootstrap Compiler\n#include \"runtime.c\"\n\n");

    // Forward declarations

    char* fwd = calloc(1, 64*1024);
    char* fns = calloc(1, 512*1024);
    char* main_code = calloc(1, 256*1024);

    for (int i = 0; i < ast.arr.len; i++) {
        HexaVal s = ast.arr.items[i];
        if (strcmp(hexa_map_get(s,"kind").s, "FnDecl") == 0) {
            // Forward decl
            strcat(fwd, "HexaVal "); strcat(fwd, hexa_map_get(s,"name").s); strcat(fwd, "(");
            HexaVal params = hexa_map_get(s,"params");
            for (int j=0; params.tag==TAG_ARRAY && j<params.arr.len; j++) {
                if (j>0) strcat(fwd,", ");
                strcat(fwd,"HexaVal "); strcat(fwd, hexa_map_get(params.arr.items[j],"name").s);
            }
            if (params.tag!=TAG_ARRAY||params.arr.len==0) strcat(fwd,"void");
            strcat(fwd, ");\n");
            // Function body
            strcat(fns, "HexaVal "); strcat(fns, hexa_map_get(s,"name").s); strcat(fns, "(");
            for (int j=0; params.tag==TAG_ARRAY && j<params.arr.len; j++) {
                if (j>0) strcat(fns,", ");
                strcat(fns,"HexaVal "); strcat(fns, hexa_map_get(params.arr.items[j],"name").s);
            }
            if (params.tag!=TAG_ARRAY||params.arr.len==0) strcat(fns,"void");
            strcat(fns, ") {\n");
            HexaVal body = hexa_map_get(s,"body");
            for (int j=0; body.tag==TAG_ARRAY && j<body.arr.len; j++) gen_stmt(body.arr.items[j], 1, fns);
            strcat(fns, "    return hexa_void();\n}\n\n");
        } else {
            gen_stmt(s, 1, main_code);
        }
    }

    strcat(buf, fwd); strcat(buf, "\n"); strcat(buf, fns);
    strcat(buf, "int main(int argc, char** argv) {\n    hexa_set_args(argc, argv);\n"); strcat(buf, main_code); strcat(buf, "    return 0;\n}\n");

    free(fwd); free(fns); free(main_code);
    return buf;
}

// ══════════════════════════════════════════════════════════
// MAIN — full pipeline: read → tokenize → parse → codegen → gcc
// ══════════════════════════════════════════════════════════


// ══════════════════════════════════════════════════════════
// ASM CODEGEN — Generate ARM64 assembly (as+ld, no gcc)
// ══════════════════════════════════════════════════════════

void gen_asm_stmt_c(HexaVal, char*, int, int*, const char*);
void gen_asm_expr_c(HexaVal, char*, int, int*, const char*);

char* codegen_asm_full(HexaVal ast) {
    char* buf = calloc(1, 512*1024);
    strcat(buf, "// Generated by HEXA Bootstrap Compiler\n");
    strcat(buf, ".global _main\n.align 4\n\n");

    // Helper: print integer
    strcat(buf,
        "_hexa_print_int:\n"
        "    stp x29, x30, [sp, #-16]!\n"
        "    mov x29, sp\n"
        "    sub sp, sp, #32\n"
        "    mov x9, x0\n"          // save original
        "    cmp x0, #0\n"
        "    b.ge .Lpi_pos\n"
        "    neg x0, x0\n"
        ".Lpi_pos:\n"
        "    add x1, sp, #20\n"     // buffer end
        "    mov x2, #0\n"          // len
        "    mov x3, #10\n"
        ".Lpi_loop:\n"
        "    udiv x4, x0, x3\n"
        "    msub x5, x4, x3, x0\n"
        "    add x5, x5, #48\n"
        "    sub x1, x1, #1\n"
        "    strb w5, [x1]\n"
        "    add x2, x2, #1\n"
        "    mov x0, x4\n"
        "    cbnz x0, .Lpi_loop\n"
        // check negative
        "    cmp x9, #0\n"
        "    b.ge .Lpi_write\n"
        "    sub x1, x1, #1\n"
        "    mov w5, #45\n"         // '-'
        "    strb w5, [x1]\n"
        "    add x2, x2, #1\n"
        ".Lpi_write:\n"
        "    mov x0, #1\n"
        "    mov x16, #4\n"
        "    svc #0x80\n"
        // newline
        "    mov w5, #10\n"
        "    strb w5, [sp]\n"
        "    mov x0, #1\n"
        "    mov x1, sp\n"
        "    mov x2, #1\n"
        "    mov x16, #4\n"
        "    svc #0x80\n"
        "    add sp, sp, #32\n"
        "    ldp x29, x30, [sp], #16\n"
        "    ret\n\n"
    );

    // Emit functions
    char* fns = calloc(1, 256*1024);
    char* main_code = calloc(1, 128*1024);
    int label_counter = 0;

    for (int i = 0; i < ast.arr.len; i++) {
        HexaVal s = ast.arr.items[i];
        const char* k = hexa_map_get(s, "kind").s;
        if (strcmp(k, "FnDecl") == 0) {
            const char* name = hexa_map_get(s, "name").s;
            HexaVal params = hexa_map_get(s, "params");
            HexaVal body = hexa_map_get(s, "body");
            int np = params.tag == TAG_ARRAY ? params.arr.len : 0;
            int stack = (np + 8) * 8;
            char tmp[64]; sprintf(tmp, "_%s:\n", name); strcat(fns, tmp);
            strcat(fns, "    stp x29, x30, [sp, #-16]!\n    mov x29, sp\n");
            sprintf(tmp, "    sub sp, sp, #%d\n", stack); strcat(fns, tmp);
            // Save params to stack
            for (int j = 0; j < np; j++) {
                sprintf(tmp, "    str x%d, [x29, #-%d]\n", j, (j+1)*8);
                strcat(fns, tmp);
            }
            // Body
            for (int j = 0; body.tag == TAG_ARRAY && j < body.arr.len; j++) {
                gen_asm_stmt_c(body.arr.items[j], fns, np, &label_counter, name);
            }
            strcat(fns, "    mov x0, #0\n");
            sprintf(tmp, "    add sp, sp, #%d\n", stack); strcat(fns, tmp);
            strcat(fns, "    ldp x29, x30, [sp], #16\n    ret\n\n");
        } else {
            gen_asm_stmt_c(s, main_code, 0, &label_counter, "_main");
        }
    }

    strcat(buf, fns);
    strcat(buf, "_main:\n    stp x29, x30, [sp, #-16]!\n    mov x29, sp\n");
    strcat(buf, main_code);
    strcat(buf, "    mov x0, #0\n    ldp x29, x30, [sp], #16\n    ret\n");

    free(fns); free(main_code);
    return buf;
}



void gen_asm_stmt_c(HexaVal node, char* buf, int np, int* lc, const char* fn_name) {
    const char* k = hexa_map_get(node, "kind").s;

    if (strcmp(k, "ExprStmt") == 0) {
        HexaVal expr = hexa_map_get(node, "left");
        if (expr.tag == TAG_STR) return;
        const char* ek = hexa_map_get(expr, "kind").s;
        if (strcmp(ek, "Call") == 0) {
            HexaVal callee = hexa_map_get(expr, "left");
            if (strcmp(hexa_map_get(callee,"kind").s, "Ident") == 0 &&
                strcmp(hexa_map_get(callee,"name").s, "println") == 0) {
                HexaVal args = hexa_map_get(expr, "args");
                if (args.tag == TAG_ARRAY && args.arr.len > 0)
                    gen_asm_expr_c(args.arr.items[0], buf, np, lc, fn_name);
                strcat(buf, "    bl _hexa_print_int\n");
                return;
            }
            // Regular function call
            HexaVal args = hexa_map_get(expr, "args");
            int nargs = args.tag == TAG_ARRAY ? args.arr.len : 0;
            // Evaluate args in reverse, push to stack, then pop into x0-x7
            for (int i = nargs - 1; i >= 0; i--) {
                gen_asm_expr_c(args.arr.items[i], buf, np, lc, fn_name);
                strcat(buf, "    str x0, [sp, #-16]!\n");
            }
            for (int i = 0; i < nargs; i++) {
                char tmp[64]; sprintf(tmp, "    ldr x%d, [sp], #16\n", i);
                strcat(buf, tmp);
            }
            char tmp[64]; sprintf(tmp, "    bl _%s\n", hexa_map_get(callee,"name").s);
            strcat(buf, tmp);
            return;
        }
        if (strcmp(ek, "IfExpr") == 0) {
            int lbl = (*lc)++;
            gen_asm_expr_c(hexa_map_get(expr,"cond"), buf, np, lc, fn_name);
            char tmp[64]; sprintf(tmp, "    cbz x0, .Lelse_%d\n", lbl); strcat(buf, tmp);
            HexaVal tb = hexa_map_get(expr, "then_body");
            for (int i = 0; tb.tag == TAG_ARRAY && i < tb.arr.len; i++)
                gen_asm_stmt_c(tb.arr.items[i], buf, np, lc, fn_name);
            sprintf(tmp, ".Lelse_%d:\n", lbl); strcat(buf, tmp);
            return;
        }
        gen_asm_expr_c(expr, buf, np, lc, fn_name);
        return;
    }

    if (strcmp(k, "ReturnStmt") == 0) {
        HexaVal ret = hexa_map_get(node, "left");
        if (ret.tag != TAG_STR || strlen(ret.s) > 0)
            gen_asm_expr_c(ret, buf, np, lc, fn_name);
        int stack = (np + 8) * 8;
        char tmp[64]; sprintf(tmp, "    add sp, sp, #%d\n    ldp x29, x30, [sp], #16\n    ret\n", stack);
        strcat(buf, tmp);
        return;
    }

    if (strcmp(k, "WhileStmt") == 0) {
        int lbl = (*lc)++;
        char tmp[64];
        sprintf(tmp, ".Lwhile_%d:\n", lbl); strcat(buf, tmp);
        gen_asm_expr_c(hexa_map_get(node,"cond"), buf, np, lc, fn_name);
        sprintf(tmp, "    cbz x0, .Lwend_%d\n", lbl); strcat(buf, tmp);
        HexaVal body = hexa_map_get(node, "body");
        for (int i = 0; body.tag == TAG_ARRAY && i < body.arr.len; i++)
            gen_asm_stmt_c(body.arr.items[i], buf, np, lc, fn_name);
        sprintf(tmp, "    b .Lwhile_%d\n.Lwend_%d:\n", lbl, lbl); strcat(buf, tmp);
        return;
    }

    // LetStmt, AssignStmt — simplified (store to stack)
    char tmp[64]; sprintf(tmp, "    // %s\n", k); strcat(buf, tmp);
}

void gen_asm_expr_c(HexaVal node, char* buf, int np, int* lc, const char* fn_name) {
    const char* k = hexa_map_get(node, "kind").s;
    if (strcmp(k, "IntLit") == 0) {
        long val = atol(hexa_map_get(node,"value").s);
        char tmp[64];
        if (val >= 0 && val < 65536) { sprintf(tmp, "    mov x0, #%ld\n", val); }
        else { sprintf(tmp, "    movz x0, #%ld\n", val & 0xFFFF); } // simplified
        strcat(buf, tmp);
        return;
    }
    if (strcmp(k, "Ident") == 0) {
        const char* name = hexa_map_get(node, "name").s;
        // Simple: load from param slot
        // This is a simplification — full version needs variable tracking
        strcat(buf, "    // load "); strcat(buf, name); strcat(buf, "\n");
        // For params: x0-x7 were saved to [x29, #-8*i]
        // We need a proper variable map, but for now just use param index
        return;
    }
    if (strcmp(k, "BinOp") == 0) {
        gen_asm_expr_c(hexa_map_get(node,"right"), buf, np, lc, fn_name);
        strcat(buf, "    str x0, [sp, #-16]!\n");  // push right
        gen_asm_expr_c(hexa_map_get(node,"left"), buf, np, lc, fn_name);
        strcat(buf, "    ldr x1, [sp], #16\n");     // pop right into x1
        // x0 = left, x1 = right
        const char* op = hexa_map_get(node, "op").s;
        if (strcmp(op,"+")==0) strcat(buf, "    add x0, x0, x1\n");
        else if (strcmp(op,"-")==0) strcat(buf, "    sub x0, x0, x1\n");
        else if (strcmp(op,"*")==0) strcat(buf, "    mul x0, x0, x1\n");
        else if (strcmp(op,"/")==0) strcat(buf, "    sdiv x0, x0, x1\n");
        else if (strcmp(op,"<=")==0) { strcat(buf, "    cmp x0, x1\n    cset x0, le\n"); }
        else if (strcmp(op,"<")==0) { strcat(buf, "    cmp x0, x1\n    cset x0, lt\n"); }
        else if (strcmp(op,">")==0) { strcat(buf, "    cmp x0, x1\n    cset x0, gt\n"); }
        else if (strcmp(op,">=")==0) { strcat(buf, "    cmp x0, x1\n    cset x0, ge\n"); }
        else if (strcmp(op,"==")==0) { strcat(buf, "    cmp x0, x1\n    cset x0, eq\n"); }
        else if (strcmp(op,"!=")==0) { strcat(buf, "    cmp x0, x1\n    cset x0, ne\n"); }
        return;
    }
    if (strcmp(k, "Call") == 0) {
        HexaVal callee = hexa_map_get(node, "left");
        HexaVal args = hexa_map_get(node, "args");
        int nargs = args.tag == TAG_ARRAY ? args.arr.len : 0;
        for (int i = nargs - 1; i >= 0; i--) {
            gen_asm_expr_c(args.arr.items[i], buf, np, lc, fn_name);
            strcat(buf, "    str x0, [sp, #-16]!\n");
        }
        for (int i = 0; i < nargs; i++) {
            char tmp[64]; sprintf(tmp, "    ldr x%d, [sp], #16\n", i);
            strcat(buf, tmp);
        }
        char tmp[64]; sprintf(tmp, "    bl _%s\n", hexa_map_get(callee,"name").s);
        strcat(buf, tmp);
        return;
    }
    if (strcmp(k, "UnaryOp") == 0) {
        gen_asm_expr_c(hexa_map_get(node,"left"), buf, np, lc, fn_name);
        if (strcmp(hexa_map_get(node,"op").s, "-") == 0)
            strcat(buf, "    neg x0, x0\n");
        return;
    }
    char tmp[64]; sprintf(tmp, "    // expr:%s\n", k); strcat(buf, tmp);
}


int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: hexa_bootstrap <file.hexa> [-o output]\n");
        printf("Compiles .hexa to native binary via C.\n");
        return 1;
    }

    const char* input = argv[1];
    const char* output = argc > 3 && strcmp(argv[2],"-o")==0 ? argv[3] : "/tmp/hexa_out";

    printf("=== HEXA Bootstrap Compiler ===\n");

    // [1] Read
    HexaVal source = hexa_read_file(hexa_str(input));
    printf("[1/5] Read: %d chars\n", hexa_len(source));

    // [2] Tokenize
    HexaVal tokens = tokenize_c(source.s);
    printf("[2/5] Tokenized: %d tokens\n", tokens.arr.len);

    // [3] Parse
    HexaVal ast = parse_c(tokens);
    printf("[3/5] Parsed: %d statements\n", ast.arr.len);

    // [4] Generate C
    char* c_code = codegen_c_full(ast);
    char c_file[256]; sprintf(c_file, "%s.c", output);
    FILE* f = fopen(c_file, "w");
    fputs(c_code, f);
    fclose(f);
    printf("[4/5] C generated: %d chars -> %s\n", (int)strlen(c_code), c_file);

    // [5] Compile — try as+ld first (no gcc needed), fall back to gcc
    int ret = 1;
    char cmd[512];

    // Generate assembly version too
    char asm_file[256]; sprintf(asm_file, "%s.s", output);
    FILE* af = fopen(asm_file, "w");
    char* asm_code = codegen_asm_full(ast);
    fputs(asm_code, af);
    fclose(af);
    printf("[5a] Assembly: %d chars -> %s\n", (int)strlen(asm_code), asm_file);
    free(asm_code);

    // Try gcc first (best optimization + full feature support)
    sprintf(cmd, "gcc -O2 -lm -I ready/self %s -o %s 2>/dev/null", c_file, output);
    ret = system(cmd);
    if (ret == 0) printf("[5b] Compiled with gcc -O2\n");

    // Fallback to as+ld if gcc not available
    if (ret != 0) {
        char obj_file2[256]; sprintf(obj_file2, "%s.o", output);
        sprintf(cmd, "as %s -o %s 2>/dev/null", asm_file, obj_file2);
        if (system(cmd) == 0) {
            char sdk2[512] = "";
            FILE* p2 = popen("xcrun --show-sdk-path 2>/dev/null", "r");
            if (p2) { fgets(sdk2, sizeof(sdk2), p2); pclose(p2); sdk2[strcspn(sdk2,"\n")] = 0; }
            if (strlen(sdk2) > 0)
                sprintf(cmd, "ld %s -o %s -lSystem -L%s/usr/lib 2>/dev/null", obj_file2, output, sdk2);
            else
                sprintf(cmd, "ld %s -o %s -lSystem 2>/dev/null", obj_file2, output);
            ret = system(cmd);
            if (ret == 0) printf("[5d] Linked with as+ld fallback\n");
        }
    }
    if (ret == 0) {
        printf("\n=== BUILD SUCCESS: %s ===\n", output);
    } else {
        printf("\n=== BUILD FAILED ===\n");
    }

    free(c_code);
    return ret;
}
