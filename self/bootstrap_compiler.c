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

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: hexa_bootstrap <file.hexa>\n");
        return 1;
    }

    printf("=== HEXA Bootstrap Compiler (C) ===\n");

    // Read source file
    HexaVal source = hexa_read_file(hexa_str(argv[1]));
    printf("[1/4] Read: %d chars from %s\n", hexa_len(source), argv[1]);

    // Tokenize
    HexaVal tokens = tokenize_c(source.s);
    printf("[2/4] Tokenized: %d tokens\n", tokens.arr.len);

    // Print first 20 tokens as sample
    int show = tokens.arr.len < 20 ? tokens.arr.len : 20;
    for (int i = 0; i < show; i++) {
        HexaVal tok = tokens.arr.items[i];
        HexaVal kind = hexa_map_get(tok, "kind");
        HexaVal val = hexa_map_get(tok, "value");
        printf("  [%d] %s = '%s'\n", i, kind.s, val.s);
    }
    if (tokens.arr.len > 20) printf("  ... (%d more)\n", tokens.arr.len - 20);

    printf("\nBootstrap lexer complete.\n");
    printf("Next: add parser + codegen to this binary for full self-hosting.\n");
    return 0;
}
