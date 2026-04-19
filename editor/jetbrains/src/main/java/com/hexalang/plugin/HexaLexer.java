package com.hexalang.plugin;

import com.intellij.lexer.LexerBase;
import com.intellij.psi.tree.IElementType;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/**
 * Simple lexer for HEXA syntax highlighting.
 * Tokenizes keywords, identifiers, literals, operators, and comments.
 */
public class HexaLexer extends LexerBase {
    private CharSequence buffer;
    private int startOffset;
    private int endOffset;
    private int currentPos;
    private int tokenStart;
    private IElementType tokenType;

    // Keywords (53 total, matching the Rust lexer)
    private static final String[] KEYWORDS = {
        // Control flow
        "if", "else", "match", "for", "while", "loop",
        // Type declarations
        "type", "struct", "enum", "trait", "impl", "dyn",
        // Functions
        "fn", "return", "yield", "async", "await",
        // Variables
        "let", "mut", "const", "static",
        // Modules
        "mod", "use", "pub", "crate",
        // Memory
        "own", "borrow", "move", "drop",
        // Concurrency
        "spawn", "channel", "select", "atomic", "scope",
        // Effects
        "effect", "handle", "resume", "pure",
        // Proofs
        "proof", "assert", "invariant", "theorem",
        // Meta
        "macro", "derive", "where", "comptime",
        // Errors
        "try", "catch", "throw", "panic", "recover",
        // AI / Consciousness
        "intent", "generate", "verify", "optimize",
        // Consciousness v2
        "consciousness", "tension_link", "evolve",
        // Boolean literals
        "true", "false",
    };

    @Override
    public void start(@NotNull CharSequence buffer, int startOffset, int endOffset, int initialState) {
        this.buffer = buffer;
        this.startOffset = startOffset;
        this.endOffset = endOffset;
        this.currentPos = startOffset;
        this.tokenType = null;
        advance();
    }

    @Override
    public int getState() {
        return 0;
    }

    @Nullable
    @Override
    public IElementType getTokenType() {
        return tokenType;
    }

    @Override
    public int getTokenStart() {
        return tokenStart;
    }

    @Override
    public int getTokenEnd() {
        return currentPos;
    }

    @Override
    public void advance() {
        if (currentPos >= endOffset) {
            tokenType = null;
            return;
        }

        tokenStart = currentPos;
        char c = buffer.charAt(currentPos);

        // Line comment
        if (c == '/' && currentPos + 1 < endOffset && buffer.charAt(currentPos + 1) == '/') {
            while (currentPos < endOffset && buffer.charAt(currentPos) != '\n') {
                currentPos++;
            }
            tokenType = HexaTokenTypes.COMMENT;
            return;
        }

        // Block comment
        if (c == '/' && currentPos + 1 < endOffset && buffer.charAt(currentPos + 1) == '*') {
            currentPos += 2;
            while (currentPos + 1 < endOffset) {
                if (buffer.charAt(currentPos) == '*' && buffer.charAt(currentPos + 1) == '/') {
                    currentPos += 2;
                    break;
                }
                currentPos++;
            }
            tokenType = HexaTokenTypes.COMMENT;
            return;
        }

        // String literal
        if (c == '"') {
            currentPos++;
            while (currentPos < endOffset && buffer.charAt(currentPos) != '"') {
                if (buffer.charAt(currentPos) == '\\') currentPos++;
                currentPos++;
            }
            if (currentPos < endOffset) currentPos++;
            tokenType = HexaTokenTypes.STRING;
            return;
        }

        // Char literal
        if (c == '\'') {
            currentPos++;
            if (currentPos < endOffset && buffer.charAt(currentPos) == '\\') currentPos++;
            if (currentPos < endOffset) currentPos++;
            if (currentPos < endOffset && buffer.charAt(currentPos) == '\'') currentPos++;
            tokenType = HexaTokenTypes.STRING;
            return;
        }

        // Number
        if (Character.isDigit(c)) {
            while (currentPos < endOffset && (Character.isDigit(buffer.charAt(currentPos))
                    || buffer.charAt(currentPos) == '.' || buffer.charAt(currentPos) == '_')) {
                currentPos++;
            }
            tokenType = HexaTokenTypes.NUMBER;
            return;
        }

        // Identifier / keyword
        if (Character.isLetter(c) || c == '_') {
            while (currentPos < endOffset && (Character.isLetterOrDigit(buffer.charAt(currentPos))
                    || buffer.charAt(currentPos) == '_')) {
                currentPos++;
            }
            String word = buffer.subSequence(tokenStart, currentPos).toString();
            tokenType = isKeyword(word) ? HexaTokenTypes.KEYWORD : HexaTokenTypes.IDENTIFIER;
            return;
        }

        // Braces / brackets / parens
        if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']') {
            currentPos++;
            tokenType = HexaTokenTypes.BRACE;
            return;
        }

        // Operators
        if ("+-*/%=<>!&|^~.,:;@#?".indexOf(c) >= 0) {
            currentPos++;
            // Multi-char operators
            if (currentPos < endOffset) {
                char next = buffer.charAt(currentPos);
                if ((c == '=' && next == '=') || (c == '!' && next == '=')
                    || (c == '<' && next == '=') || (c == '>' && next == '=')
                    || (c == '&' && next == '&') || (c == '|' && next == '|')
                    || (c == '-' && next == '>') || (c == '=' && next == '>')
                    || (c == '.' && next == '.') || (c == ':' && next == ':')
                    || (c == '*' && next == '*') || (c == ':' && next == '=')) {
                    currentPos++;
                }
            }
            tokenType = HexaTokenTypes.OPERATOR;
            return;
        }

        // Whitespace
        if (Character.isWhitespace(c)) {
            while (currentPos < endOffset && Character.isWhitespace(buffer.charAt(currentPos))) {
                currentPos++;
            }
            tokenType = HexaTokenTypes.WHITESPACE;
            return;
        }

        // Unknown character
        currentPos++;
        tokenType = HexaTokenTypes.BAD_CHARACTER;
    }

    @NotNull
    @Override
    public CharSequence getBufferSequence() {
        return buffer;
    }

    @Override
    public int getBufferEnd() {
        return endOffset;
    }

    private boolean isKeyword(String word) {
        for (String kw : KEYWORDS) {
            if (kw.equals(word)) return true;
        }
        return false;
    }
}
