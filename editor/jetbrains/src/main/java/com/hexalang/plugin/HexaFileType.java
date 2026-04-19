package com.hexalang.plugin;

import com.intellij.openapi.fileTypes.LanguageFileType;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.*;

/**
 * File type for .hexa files.
 */
public class HexaFileType extends LanguageFileType {
    public static final HexaFileType INSTANCE = new HexaFileType();

    private HexaFileType() {
        super(HexaLanguage.INSTANCE);
    }

    @NotNull
    @Override
    public String getName() {
        return "HEXA";
    }

    @NotNull
    @Override
    public String getDescription() {
        return "HEXA language file";
    }

    @NotNull
    @Override
    public String getDefaultExtension() {
        return "hexa";
    }

    @Nullable
    @Override
    public Icon getIcon() {
        return null; // TODO: Add HEXA icon
    }
}
