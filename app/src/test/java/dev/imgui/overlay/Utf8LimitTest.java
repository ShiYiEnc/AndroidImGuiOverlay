package dev.imgui.overlay;

import android.text.Spanned;
import org.junit.Test;
import java.lang.reflect.Proxy;
import static org.junit.Assert.*;

public class Utf8LimitTest {
    private Spanned span(String value) {
        return (Spanned) Proxy.newProxyInstance(Spanned.class.getClassLoader(), new Class<?>[]{Spanned.class}, (proxy, method, args) -> {
            switch (method.getName()) {
                case "length": return value.length();
                case "charAt": return value.charAt((Integer) args[0]);
                case "subSequence": return value.subSequence((Integer) args[0], (Integer) args[1]);
                case "toString": return value;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
    }
    @Test public void permitsChineseAtExactByteLimit() {
        assertNull(new OverlayService.Utf8Limit(6).filter("中文", 0, 2, span(""), 0, 0));
    }
    @Test public void rejectsWholeEmojiWithoutSplittingSurrogates() {
        assertEquals("", new OverlayService.Utf8Limit(3).filter("\uD83D\uDE00", 0, 2, span(""), 0, 0).toString());
    }
    @Test public void permitsReplacingSelectedTextWithinCapacity() {
        assertNull(new OverlayService.Utf8Limit(4).filter("文", 0, 1, span("abcX"), 0, 3));
    }
    @Test public void rejectedReplacementPreservesOriginalSelection() {
        assertEquals("abc", new OverlayService.Utf8Limit(4).filter("中文", 0, 2, span("abcX"), 0, 3).toString());
    }
    @Test public void deletingTextAlwaysFits() {
        assertNull(new OverlayService.Utf8Limit(6).filter("", 0, 0, span("中文"), 0, 1));
    }
    @Test public void checksOnlyIncomingCompositionSlice() {
        assertNull(new OverlayService.Utf8Limit(3).filter("X文Y", 1, 2, span(""), 0, 0));
    }
}
