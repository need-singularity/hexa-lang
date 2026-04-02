package com.hexalang.plugin;

import com.intellij.psi.tree.IElementType;

/**
 * Token types for HEXA syntax highlighting.
 */
public class HexaTokenTypes {
    public static final IElementType KEYWORD = new IElementType("HEXA_KEYWORD", HexaLanguage.INSTANCE);
    public static final IElementType IDENTIFIER = new IElementType("HEXA_IDENTIFIER", HexaLanguage.INSTANCE);
    public static final IElementType NUMBER = new IElementType("HEXA_NUMBER", HexaLanguage.INSTANCE);
    public static final IElementType STRING = new IElementType("HEXA_STRING", HexaLanguage.INSTANCE);
    public static final IElementType COMMENT = new IElementType("HEXA_COMMENT", HexaLanguage.INSTANCE);
    public static final IElementType OPERATOR = new IElementType("HEXA_OPERATOR", HexaLanguage.INSTANCE);
    public static final IElementType BRACE = new IElementType("HEXA_BRACE", HexaLanguage.INSTANCE);
    public static final IElementType WHITESPACE = new IElementType("HEXA_WHITESPACE", HexaLanguage.INSTANCE);
    public static final IElementType BAD_CHARACTER = new IElementType("HEXA_BAD_CHARACTER", HexaLanguage.INSTANCE);
}
