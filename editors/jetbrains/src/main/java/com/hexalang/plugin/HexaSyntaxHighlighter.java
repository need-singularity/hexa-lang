package com.hexalang.plugin;

import com.intellij.lexer.Lexer;
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors;
import com.intellij.openapi.editor.colors.TextAttributesKey;
import com.intellij.openapi.fileTypes.SyntaxHighlighterBase;
import com.intellij.psi.tree.IElementType;
import org.jetbrains.annotations.NotNull;

import static com.intellij.openapi.editor.colors.TextAttributesKey.createTextAttributesKey;

/**
 * Syntax highlighter for HEXA files.
 */
public class HexaSyntaxHighlighter extends SyntaxHighlighterBase {

    public static final TextAttributesKey KEYWORD_KEY =
        createTextAttributesKey("HEXA_KEYWORD", DefaultLanguageHighlighterColors.KEYWORD);
    public static final TextAttributesKey STRING_KEY =
        createTextAttributesKey("HEXA_STRING", DefaultLanguageHighlighterColors.STRING);
    public static final TextAttributesKey NUMBER_KEY =
        createTextAttributesKey("HEXA_NUMBER", DefaultLanguageHighlighterColors.NUMBER);
    public static final TextAttributesKey COMMENT_KEY =
        createTextAttributesKey("HEXA_COMMENT", DefaultLanguageHighlighterColors.LINE_COMMENT);
    public static final TextAttributesKey OPERATOR_KEY =
        createTextAttributesKey("HEXA_OPERATOR", DefaultLanguageHighlighterColors.OPERATION_SIGN);
    public static final TextAttributesKey BRACE_KEY =
        createTextAttributesKey("HEXA_BRACE", DefaultLanguageHighlighterColors.BRACES);
    public static final TextAttributesKey IDENTIFIER_KEY =
        createTextAttributesKey("HEXA_IDENTIFIER", DefaultLanguageHighlighterColors.IDENTIFIER);
    public static final TextAttributesKey BAD_CHAR_KEY =
        createTextAttributesKey("HEXA_BAD_CHARACTER", DefaultLanguageHighlighterColors.INVALID_STRING_ESCAPE);

    private static final TextAttributesKey[] KEYWORD_KEYS = new TextAttributesKey[]{KEYWORD_KEY};
    private static final TextAttributesKey[] STRING_KEYS = new TextAttributesKey[]{STRING_KEY};
    private static final TextAttributesKey[] NUMBER_KEYS = new TextAttributesKey[]{NUMBER_KEY};
    private static final TextAttributesKey[] COMMENT_KEYS = new TextAttributesKey[]{COMMENT_KEY};
    private static final TextAttributesKey[] OPERATOR_KEYS = new TextAttributesKey[]{OPERATOR_KEY};
    private static final TextAttributesKey[] BRACE_KEYS = new TextAttributesKey[]{BRACE_KEY};
    private static final TextAttributesKey[] IDENTIFIER_KEYS = new TextAttributesKey[]{IDENTIFIER_KEY};
    private static final TextAttributesKey[] BAD_CHAR_KEYS = new TextAttributesKey[]{BAD_CHAR_KEY};
    private static final TextAttributesKey[] EMPTY_KEYS = new TextAttributesKey[0];

    @NotNull
    @Override
    public Lexer getHighlightingLexer() {
        return new HexaLexer();
    }

    @NotNull
    @Override
    public TextAttributesKey @NotNull [] getTokenHighlights(IElementType tokenType) {
        if (tokenType.equals(HexaTokenTypes.KEYWORD)) return KEYWORD_KEYS;
        if (tokenType.equals(HexaTokenTypes.STRING)) return STRING_KEYS;
        if (tokenType.equals(HexaTokenTypes.NUMBER)) return NUMBER_KEYS;
        if (tokenType.equals(HexaTokenTypes.COMMENT)) return COMMENT_KEYS;
        if (tokenType.equals(HexaTokenTypes.OPERATOR)) return OPERATOR_KEYS;
        if (tokenType.equals(HexaTokenTypes.BRACE)) return BRACE_KEYS;
        if (tokenType.equals(HexaTokenTypes.IDENTIFIER)) return IDENTIFIER_KEYS;
        if (tokenType.equals(HexaTokenTypes.BAD_CHARACTER)) return BAD_CHAR_KEYS;
        return EMPTY_KEYS;
    }
}
