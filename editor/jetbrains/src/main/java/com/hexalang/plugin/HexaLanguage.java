package com.hexalang.plugin;

import com.intellij.lang.Language;

/**
 * HEXA language definition for IntelliJ platform.
 */
public class HexaLanguage extends Language {
    public static final HexaLanguage INSTANCE = new HexaLanguage();

    private HexaLanguage() {
        super("HEXA");
    }
}
