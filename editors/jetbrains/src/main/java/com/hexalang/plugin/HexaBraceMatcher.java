package com.hexalang.plugin;

import com.intellij.lang.BracePair;
import com.intellij.lang.PairedBraceMatcher;
import com.intellij.psi.PsiFile;
import com.intellij.psi.tree.IElementType;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

public class HexaBraceMatcher implements PairedBraceMatcher {
    // Brace matching is handled at the token level
    // The BRACE token type covers {}, (), []

    @NotNull
    @Override
    public BracePair @NotNull [] getPairs() {
        return new BracePair[0]; // Simplified -- full matching needs separate token types per brace
    }

    @Override
    public boolean isPairedBracesAllowedBeforeType(@NotNull IElementType lbraceType, @Nullable IElementType contextType) {
        return true;
    }

    @Override
    public int getCodeConstructStart(PsiFile file, int openingBraceOffset) {
        return openingBraceOffset;
    }
}
