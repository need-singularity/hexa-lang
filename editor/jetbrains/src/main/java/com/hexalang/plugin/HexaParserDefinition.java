package com.hexalang.plugin;

import com.intellij.lang.ASTNode;
import com.intellij.lang.ParserDefinition;
import com.intellij.lang.PsiParser;
import com.intellij.lexer.Lexer;
import com.intellij.openapi.project.Project;
import com.intellij.psi.FileViewProvider;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.tree.IFileElementType;
import com.intellij.psi.tree.TokenSet;
import org.jetbrains.annotations.NotNull;

/**
 * Minimal parser definition for HEXA.
 * Full parsing is done by the Rust-based hexa compiler;
 * this provides enough for IDE syntax highlighting and structure.
 */
public class HexaParserDefinition implements ParserDefinition {
    public static final IFileElementType FILE = new IFileElementType(HexaLanguage.INSTANCE);

    @NotNull
    @Override
    public Lexer createLexer(Project project) {
        return new HexaLexer();
    }

    @NotNull
    @Override
    public PsiParser createParser(Project project) {
        // Minimal parser -- real parsing done by hexa compiler
        throw new UnsupportedOperationException("Full parsing requires hexa compiler");
    }

    @NotNull
    @Override
    public IFileElementType getFileNodeType() {
        return FILE;
    }

    @NotNull
    @Override
    public TokenSet getCommentTokens() {
        return TokenSet.create(HexaTokenTypes.COMMENT);
    }

    @NotNull
    @Override
    public TokenSet getStringLiteralElements() {
        return TokenSet.create(HexaTokenTypes.STRING);
    }

    @NotNull
    @Override
    public TokenSet getWhitespaceTokens() {
        return TokenSet.create(HexaTokenTypes.WHITESPACE);
    }

    @NotNull
    @Override
    public PsiElement createElement(ASTNode node) {
        throw new UnsupportedOperationException("Not yet implemented");
    }

    @NotNull
    @Override
    public PsiFile createFile(@NotNull FileViewProvider viewProvider) {
        throw new UnsupportedOperationException("Not yet implemented");
    }
}
