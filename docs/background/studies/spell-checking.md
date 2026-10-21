# Spell Checking

The Windows Spell Checking API behind TextBox and RichEditBox.

The question this study answers is what it takes to give a text field OS-backed spell checking that meets four requirements: follow the user's Windows settings, highlight misspelled words, offer a right-click correction menu, and autocorrect when the user has that switched on. The short answer is to call the Windows Spell Checking API (`ISpellCheckerFactory`/`ISpellChecker`, `spellcheck.h`) directly from the control's own code. RichEdit's built-in checker looks like the shortcut, but it fails the menu requirement outright and the language requirement partly.

Every API claim below was either taken from Microsoft's reference documentation or checked with a standalone probe on a real Windows install. Where the evidence is only community documentation (the registry values behind the Settings switches, for example), the page says so.

## The API

The Spell Checking API ships with Windows 8 and Windows Server 2012 and later. It is a plain in-process COM server usable from desktop apps; no manifest and no package identity are needed[^factory]:

```cpp
ComPtr<ISpellCheckerFactory> factory;
CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER,
                 IID_PPV_ARGS(&factory));
```

`ISpellCheckerFactory`[^factory]:

| Method | Purpose |
| --- | --- |
| `get_SupportedLanguages()` | An `IEnumString` of the BCP-47 tags this machine has a checker for |
| `IsSupported(languageTag)` | A cheap check before committing to a language |
| `CreateSpellChecker(languageTag)` | Returns the preferred `ISpellChecker` for the language, according to the user's ranking[^create] |

`ISpellChecker`[^checker]:

| Method | Purpose |
| --- | --- |
| `Check(text)` | Returns an `IEnumSpellingError`; the per-edit check |
| `ComprehensiveCheck(text)` | Documented as "more thorough" than `Check`; not probed |
| `Suggest(word)` | Ranked replacement candidates for one word |
| `Add(word)` | Treats the word as correctly spelled from now on[^add] |
| `Ignore(word)` | The same, for this session only |
| `AutoCorrect(from, to)` | Registers a standing correction[^autocorrect] |
| `get_Id()`, `get_LocalizedName()`, `get_LanguageTag()` | Identify the engine and language in use |
| `GetOptionIds()`, `GetOptionDescription()`, `GetOptionValue()` | Per-engine options; not needed for a text field |
| `add_SpellCheckerChanged` / `remove_SpellCheckerChanged` | Fires when the user changes the default engine while the app runs |

Each `ISpellingError`[^error] carries `StartIndex` and `Length` in UTF-16 code units, a `CORRECTIVE_ACTION`[^action], and a `Replacement` that is only valid for the replace action:

```cpp
enum CORRECTIVE_ACTION {
  CORRECTIVE_ACTION_NONE = 0,             // no error
  CORRECTIVE_ACTION_GET_SUGGESTIONS = 1,  // prompt the user with Suggest()'s list
  CORRECTIVE_ACTION_REPLACE = 2,          // apply Replacement without prompting
  CORRECTIVE_ACTION_DELETE = 3,           // prompt to delete, e.g. a repeated word
};
```

### What the Probe Confirmed

- `CreateSpellChecker(L"en-US")` succeeded. `get_Id()` returned `MsSpell` and `get_LocalizedName()` returned `Microsoft Windows Spellchecker`, the same engine behind WinRT's `Windows.Data.Text.SpellChecker`.
- `get_SupportedLanguages()` returned 24 tags, exactly the proofing tools installed on that machine (`de-AT de-CH de-DE de-LI de-LU en-029 en-AU en-BZ en-CA en-GB en-HK en-ID en-IE en-IN en-JM en-LR en-MY en-NZ en-PH en-SG en-TT en-US en-ZA en-ZW`). A language without a proofing tool is simply absent, and `CreateSpellChecker` for it fails with `E_INVALIDARG`[^create].
- `Check()` on "Ths is a tset of the speling checker, wich shoud find erors." found all six planted errors, each as `CORRECTIVE_ACTION_GET_SUGGESTIONS`. The suggestions are sensible but the right word is not always first: `wich` gave `witch, which, winch, wish, with`.
- After `AutoCorrect(L"teh", L"the")`, `Check(L"I saw teh dog.")` returned one error with `CORRECTIVE_ACTION_REPLACE` and `Replacement = "the"`. So once a correction is registered, `Check()` itself reclassifies future occurrences; a caller only has to branch on the corrective action.
- `Ignore(L"asdkjqwe")` removed the word from the next `Check()` entirely (one error before, none after).

## TextBox Versus RichEditBox

The two kinds of text field a library like libwinui can offer sit at opposite ends for this job.

A **framework-drawn text box** (`ui::TextBox`) has no `HWND`. It renders its own text and owns every keystroke, so it already does the two hard parts: it knows exactly where each character sits (the same prefix-width measurement its hit-testing and selection highlight use), and it sees every edit as it happens. Turning an error's `[start, start + length)` into an `x0..x1` span for a squiggle is the same substring measurement the selection highlight already performs.

A **native RichEdit control** (`RICHEDIT50W` from `Msftedit.dll`) can switch on its own checker with `EM_SETLANGOPTIONS` and `IMF_SPELLCHECKING`, plus `EM_SETEDITSTYLE` with `SES_USECTF | SES_CTFALLOWEMBED | SES_CTFALLOWSMARTTAG | SES_CTFALLOWPROOFING` for the Text Services Framework[^richedit]. One message and done, in theory. It fails two requirements:

- **The menu.** With the checker active, RichEdit does not call `IRichEditOleCallback::GetContextMenu`. The RichEdit team said so in 2014, and added that the one escape hatch, `GCMF_SPELLING` in an extended `GETCONTEXTMENUEX`, is "only used in Office 2013 and immersive builds of RichEdit", not in `msftedit.dll`[^richedit]. So the built-in checker hands you Windows' own closed menu, with no way to extend or replace it.
- **The language.** A later comment on the same post reports that the built-in checker is only active when the input language is the system's default language, and that text already present is not checked until it is edited[^richedit]. Community-reported, not documented.

The conclusion was that the framework does the checking and the drawing, for every control that can show a per-range mark.

### What libwinui Does Now

- `ui::TextBox` and the framework-drawn `ui::RichEditBox` (`include/winui/ui/text/RichEditBox.hpp`) both check through the window's `SpellChecker` and draw their own squiggles.
- `native_controls::RichEditBox` (example code), the native `RICHEDIT50W` wrapper, turned out to need the framework's checker as well. `IMF_SPELLCHECKING` measured inert for a plain `HWND` host, so the control asks the same `SpellChecker` for errors and marks each range with `CFU_UNDERLINEWAVE`. RichEdit 8 accepts `CHARFORMAT2W::bUnderlineColor` but discards it, and snaps `\ulc` in RTF onto a fixed 16-color palette, so the only way to get a red wave is to stream each marked word back over itself as RTF (`\ulwave\ulc1` with a pure 255,0,0 color table entry) with `SFF_SELECTION`. The text stays byte-identical and the marks stay off the undo stack, but the red cannot be themed and does not match the color `TextBox` uses. It also checks on every `EN_CHANGE` without a debounce, which is fine for the paragraph-sized text a native escape hatch holds.
- `native_controls::EditBox` (example code), a plain `EDIT` control, has no spell checking.

### Marking Errors in a Native RichEdit

Getting a red wave into `RICHEDIT50W` took a few measured workarounds, all in `examples/native_controls/rich_edit_box.cpp`:

- **Advanced typography is required.** Without `EM_SETTYPOGRAPHYOPTIONS` and `TO_ADVANCEDTYPOGRAPHY`, RichEdit silently draws `CFU_UNDERLINEWAVE` as a straight `CFU_UNDERLINE`.
- **The color palette is fixed.** RichEdit snaps `\ulc` onto 16 colors (black, blue, cyan, green, magenta, red, yellow, white, then their dark halves). Any other color falls back to "no color" and the wave draws in the text color.
- **The fragment must reproduce the word exactly.** `SFF_SELECTION` replaces the selection with the streamed RTF, so each word is written back exactly: printable ASCII as is, RTF's three structural characters `\ { }` escaped, and every other UTF-16 code unit as `\uN?` (N signed, so a surrogate comes out negative). The fragment carries no `\fonttbl`, `\fs` or `\cf`, so the word keeps its font, size and color, and the viewport does not scroll. Marks go on last error first, so even a round trip that changed a word's length would only shift offsets already marked.
- **Marks stay off the undo stack.** Character formatting is an ordinary undoable edit, so without care Ctrl+Z would un-mark a typo instead of undoing typing. `ITextDocument::Undo(tomSuspend)` (through `EM_GETOLEINTERFACE`) suspends undo while the marks go on; a probe confirmed `EM_CANUNDO` stays 0. `EN_CHANGE` is silenced for the same stretch, or the pass would schedule itself again.
- **Every pass starts clean.** One `EM_SETCHARFORMAT` clears all previous marks instead of diffing them, because RichEdit carries formatting forward at the insertion point: text typed onto the end of a marked word would inherit the wave.

The pass re-runs when the text changes or `SpellChecker::generation()` moves, so "Add to dictionary" in one field clears the squiggle here too. Turning highlighting off still runs a pass, which clears the drawn marks. A correction picked from the menu goes through `EM_REPLACESEL` with `wParam` `TRUE`, so it lands on RichEdit's own undo stack like typing would. Wiring the same checker into `native_controls::EditBox` worked (the same `check()` call found the same errors), but an `EDIT` has one font and one color for its whole buffer, so it cannot show a mark, and a suggestion menu over text that looks correct is a menu nobody would open.

## Four Requirements

### 1. Follow the User's Windows Settings

Two different things count as "the user's settings": which language to check, and whether highlighting and autocorrect are switched on at all.

**Language.** There is no "the user's spell-check language" API. The practical signal is the current keyboard layout, converted to a BCP-47 tag:

```cpp
HKL    hkl    = GetKeyboardLayout(0);
LANGID langid = LOWORD(reinterpret_cast<uintptr_t>(hkl));
wchar_t tag[LOCALE_NAME_MAX_LENGTH];
LCIDToLocaleName(MAKELCID(langid, SORT_DEFAULT), tag, LOCALE_NAME_MAX_LENGTH, 0);
```

The probe resolved `0x0409` to `en-US`, and `IsSupported(tag)` then says whether a dictionary is installed. A user can switch keyboards mid-session, so the tag has to be re-derived on focus and on `WM_INPUTLANGCHANGE`, not cached for the process. When no dictionary exists for the layout, the right answer is to stop checking rather than to fall back to some other language.

**On/off switches.** Settings, Time & Language, Typing has two independent switches, "Highlight misspelled words" and "Autocorrect misspelled words". They are backed by undocumented registry values[^switches]:

```text
HKEY_CURRENT_USER\Software\Microsoft\TabletTip\1.7
    EnableSpellchecking     REG_DWORD   0 = off, 1 or absent = on   (Highlight misspelled words)
    EnableAutocorrection    REG_DWORD   0 = off, 1 or absent = on   (Autocorrect misspelled words)
```

The Spell Checking API does not read these itself. They govern Microsoft's own text stack; a Win32 app calling `ISpellChecker` gets checking and suggestions regardless, unless it reads and honors the values. So "follow the Windows settings" is something the framework has to implement. Because the values are undocumented, a missing value should read as "on", Windows' own default, so a future Windows that drops them fails open.

### 2. Highlight Misspelled Words

Neither Direct2D nor DirectWrite has a stock squiggle; Office draws its own. A misspelling underline is therefore a small render primitive of its own rather than something a control assembles from line segments. That keeps a semantic golden test at one readable "squiggle under `[x0, x1)`" command.

The timing matters more than the drawing. `Check()` took about 20 ms for a 62-character sentence with six errors (see [threading](#threading)). Running that on every keystroke would add visible latency in a long field. The shape real editors use: check only the word just finished, synchronously (bounded cost), and debounce a full-field check to a couple of hundred milliseconds after the last keystroke.

### 3. A Right-Click Correction Menu

There are two ways to show one:

1. **A real Win32 popup menu** (`CreatePopupMenu`, `AppendMenuW`, `TrackPopupMenuEx`) against the window's `HWND`, at the click position. This is what every native text field does for its spelling menu (Notepad, Edge, Word), and it brings OS theming, keyboard navigation and screen-reader support for free. A headless test can still assert which items were requested, just not how the OS drew them.
2. **An in-tree popup** anchored at a zero-size rectangle at the click point. That stays inside the testable render-command world, at the cost of reimplementing menu chrome and keyboard navigation and of looking different from every other spelling menu on the system.

A spelling menu is a flat list: suggestions, "Add to dictionary", "Ignore", no submenus, no icons. It does not need what option 2 buys, so option 1 won.

### 4. Autocorrect

Mechanically this is simple, given the probe's finding that a registered correction comes back from `Check()` as `CORRECTIVE_ACTION_REPLACE` with the replacement filled in. The open question is where the correction table comes from, and the probe found no built-in one: `wich` and `shoud`, classic Word autocorrect entries, came back as `GET_SUGGESTIONS` until registered explicitly, and the interface has no method to fetch a default table. Put together with the settings finding above:

- The "Autocorrect misspelled words" setting does not mean the OS will fix common typos for a third-party app. Microsoft's own apps keep their own lists, which this API does not expose. An app that wants autocorrect has to own its table.
- The setting should still be honored as a "no", but switching it on produces no corrections by itself.
- Autocorrect fires when a word is committed (space, punctuation, Enter), never inside a word being typed. A half-typed word corrected out from under the user is exactly why autocorrect has a bad reputation.
- A correction the user immediately undoes should not fire again. Word's convention is to revert once and leave the word alone after that.

## The SpellChecker Seam

The study proposed the same seam shape the library already used for the clipboard: an abstract interface a control depends on, a real Win32 implementation, and a headless stand-in for tests. That is what shipped, in `include/winui/render/SpellChecker.hpp`:

```cpp
class SpellChecker
{
public:
    virtual ~SpellChecker() = default;

    [[nodiscard]] virtual bool          available() const           = 0;
    [[nodiscard]] virtual std::string   language() const            = 0;
    [[nodiscard]] virtual bool          highlight_enabled() const   = 0;
    [[nodiscard]] virtual bool          autocorrect_enabled() const = 0;
    [[nodiscard]] virtual std::uint64_t generation() const          = 0;

    [[nodiscard]] virtual std::vector<SpellingError> check(std::string_view text)   = 0;
    [[nodiscard]] virtual std::vector<std::string>   suggest(std::string_view word) = 0;

    virtual void add_to_dictionary(std::string_view word)                      = 0;
    virtual void ignore_for_session(std::string_view word)                     = 0;
    virtual void add_autocorrection(std::string_view from, std::string_view to) = 0;
};
```

A `SpellingError` has a `start`, a `length`, a `SpellingAction` (`Suggest`, `Replace` or `Delete`; the API's `NONE` never reaches a caller) and a `replacement`.

- **`ui::win32::Win32SpellChecker`** (`src/platform/windows/Win32SpellChecker.hpp`) wraps the COM API behind a pimpl and owns the policy the API leaves to its caller. It follows the keyboard layout (`refresh_language()`, which the Win32 host calls on `WM_INPUTLANGCHANGE` and `WM_SETFOCUS`) unless constructed with a pinned tag, and reads the two Typing switches (`refresh_settings()`, called on `WM_SETTINGCHANGE`). It keeps one `ISpellChecker` per language for the process. The API speaks UTF-16; the wrapper maps every error range back to UTF-8 byte offsets into the checked text.
- **`generation()`** is bumped whenever something happened that can change an earlier answer: a word added or ignored, the language switched, a setting flipped. A control remembers the generation its errors were computed at and re-checks once it moves on. That is how "Add to dictionary" in one field clears the same squiggle in every other field.
- **Autocorrections are app-owned and in-process.** `add_autocorrection()` does not call `ISpellChecker::AutoCorrect()`; the table lives in the wrapper, so an app's corrections are not written into the user's own list. `Win32SpellChecker` seeds a small table of classic English typos (`teh`, `recieve`, `seperate`, `definately` and so on) when the active language is English, and only applies an entry when the OS dictionary actually flags the word, so a seed that happens to be valid in some variant is never applied.
- **`ui::testing::TestSpellChecker`** (`include/winui/testing/TestSpellChecker.hpp`) stands in for tests with a canned dictionary (`add_misspelling("Ths", {"This"})`).

The host installs one checker per window with `Window::set_spell_checker()` (non-owning), or a source through `Window::set_spell_checker_source()` that creates it the first time a control needs one, and the window hands it to the focused control through `Element::sync_spell_checker()`. The Win32 host does this automatically, configured through `ui::WindowOptions::spell_check`:

```cpp
ui::WindowOptions options;
options.spell_check.autocorrections = {{"libwnui", "libwinui"}, {"widgit", "widget"}};
ui::Window window{options};
return ui::run(window);
```

A single field opts out with `TextBoxControl::set_spell_check(false)` or `ui::TextBox{.spell_check = false}` or `ui::TextBox{.spell_check = false}`.

The right-click menu is a second seam, `ContextMenuHost` in `include/winui/render/ContextMenuHost.hpp`. A control builds a flat list of `ContextMenuItem`s and calls `show()`. `win32::Win32ContextMenuHost` turns it into `TrackPopupMenuEx` against the window's `HWND`, and `TestContextMenuHost` records the items and answers with a scripted choice. A control never caches this host: the window passes it into `Element::handle_context_menu()`, the only moment a control may show a menu. That answers the study's open question of how an `HWND`-free element reaches one `HWND`-scoped action without reaching upward for a window pointer.

### How `TextBox` Uses It

| Trigger | What happens |
| --- | --- |
| A character that ends a word (space, punctuation) | That word is checked synchronously, so its squiggle appears the moment it is finished. A `Replace` result is applied on the spot if autocorrect is on. |
| Any edit | Squiggles the edit touched are dropped, later ones shift, and a full-field check is re-armed for `TextBoxControl::SpellCheckDelayMs` (250 ms), counted down by `tick_override()`. |
| `generation()` moved on | The next tick re-checks, focused or not. |
| Ctrl+Z as the very next key after an autocorrection | The typed word comes back and is not autocorrected again in this field. |
| Right-click, Shift+F10 or the Menu key | Up to `MaxSpellingSuggestions` (5) suggestions, "Add to dictionary" and "Ignore" when the point is on a squiggle, then Cut, Copy, Paste and Select All. |

The word still being typed is never squiggled; nobody wants "Hel" flagged on the way to "Hello". Squiggles are `StrokeWavyLine` render commands in `Theme::spelling_error`, a 1 DIP stroke swinging 1 DIP either side of its axis with a 4 DIP wavelength, and they are gated on "Highlight misspelled words".

## Threading

The probe ran entirely inside `COINIT_MULTITHREADED`, and every call succeeded. So no single-threaded-apartment handling is needed, unlike `IFileDialog`, which requires an STA. `Win32SpellChecker` initializes COM lazily on first use, on the UI thread, in the multithreaded apartment. In practice a GUI thread is usually a single-threaded apartment by the time the first check runs (the text input framework and OLE initialize it that way), so the wrapper's `CoInitializeEx` gets `RPC_E_CHANGED_MODE`. That is fine: the factory is free-threaded, so it works from an STA as well, and the wrapper then just does not own the initialization. Every call has to come from that same thread.

Measured on a warm, already-booted machine, in a fresh process:

| Call | Time |
| --- | --- |
| `CreateSpellChecker(L"en-US")`, first call in the process | 6.12 ms |
| `Check()`, 62 characters, 6 errors, first call | 20.57 ms |
| `Check()`, same sentence, second call | 19.80 ms |

No cold-dictionary cliff showed up: both `Check()` calls cost the same, and creating the checker was quick. That is one data point on a machine that was already typing in that language, so the dictionary may well have been resident from other processes; a cold boot or an unusual language could look different. The design does not depend on the number staying low though. At 20 ms, a synchronous full check on every keystroke is a bad trade anyway, and the per-word plus debounced split avoids it. Whether a repeated `CreateSpellChecker` for an already-open language is cheap was not measured cleanly, which is one reason the wrapper simply keeps one checker per language.

### Open Points

- **`ComprehensiveCheck` versus `Check`.** "More thorough" is all the documentation says. Whether it catches homophones like their/there was not probed.
- **Persistence of `Add()`.** Community sources describe additions as going to a per-user store shared with other apps (the same "Add to dictionary" Word shows). `IUserDictionariesRegistrar`, which manages named custom dictionaries, was not explored.
- **Mixed languages in one field.** One `ISpellChecker` checks one language. A proper noun or a quoted foreign phrase is flagged, exactly as RichEdit's built-in checker does. The API offers no per-run language detection, so this is a limitation to document rather than a gap to chase.

## Probe Log

The probe was a single C++ file compiled with MSVC (`cl /nologo /std:c++20 /EHsc /W3 spellcheck_probe.cpp /link ole32.lib oleaut32.lib user32.lib`). It created the factory in the MTA, listed the supported languages, resolved the keyboard layout, created an `en-US` checker, ran `Check()` twice on the sample sentence with the first five suggestions per error, then registered an autocorrection and tested `Ignore()`:

```text
CoInitializeEx(MTA): hr=0x00000000
CoCreateInstance(SpellCheckerFactory): hr=0x00000000
get_SupportedLanguages: hr=0x00000000
Supported languages: de-AT de-CH de-DE de-LI de-LU en-029 en-AU en-BZ en-CA en-GB en-HK en-ID en-IE en-IN en-JM en-LR en-MY en-NZ en-PH en-SG en-TT en-US en-ZA en-ZW
Current keyboard layout langid=0x0409 -> locale name: en-US (LCIDToLocaleName ok=6)
IsSupported(current keyboard locale): hr=0x00000000
  supported=1
CreateSpellChecker(en-US): hr=0x00000000
  CreateSpellChecker took 6.12 ms
  Id=MsSpell LocalizedName=Microsoft Windows Spellchecker
First Check() call:
  Check: hr=0x00000000
  error: "Ths" [0,3) action=GET_SUGGESTIONS
    suggestions: "This" "Thus" "Thys" "The" "These"
  error: "tset" [9,4) action=GET_SUGGESTIONS
    suggestions: "test" "stet" "set"
  error: "speling" [21,7) action=GET_SUGGESTIONS
    suggestions: "spelling" "spieling" "sperling" "spilling" "spellings"
  error: "wich" [38,4) action=GET_SUGGESTIONS
    suggestions: "witch" "which" "winch" "wish" "with"
  error: "shoud" [43,5) action=GET_SUGGESTIONS
    suggestions: "should" "shroud" "shoed" "shod" "shoad"
  error: "erors" [54,5) action=GET_SUGGESTIONS
    suggestions: "errors" "error's" "errs" "errors'"
  first Check() wall time: 20.57 ms
Second Check() call (same checker, warm):
  (same six errors and suggestions)
  second Check() wall time: 19.80 ms

AutoCorrect("teh" -> "the"): hr=0x00000000
Check() after AutoCorrect registration, on "I saw teh dog.":
  Check: hr=0x00000000
  error: "teh" [6,3) action=REPLACE replacement="the"

Check() on "asdkjqwe" before Ignore():
  Check: hr=0x00000000
  error: "asdkjqwe" [0,8) action=GET_SUGGESTIONS
    suggestions: "Adjei" "Adeje"
Ignore("asdkjqwe"): hr=0x00000000
Check() on "asdkjqwe" after Ignore():
  Check: hr=0x00000000
```

The `[start,length)` pairs in the log are UTF-16 offsets as the API reports them. The process crashed during teardown, after every result above had printed. The cause was in the probe itself: it called `CoUninitialize()` while `ComPtr` locals still held the factory and checker, so their `Release()` ran after COM was gone. Keep the COM objects' lifetime inside the COM initialization, which is what `Win32SpellChecker`'s destructor does.

## Sources

[^factory]: Microsoft Learn, [ISpellCheckerFactory interface (spellcheck.h)](https://learn.microsoft.com/en-us/windows/win32/api/spellcheck/nn-spellcheck-ispellcheckerfactory).
[^create]: Microsoft Learn, [ISpellCheckerFactory::CreateSpellChecker](https://learn.microsoft.com/en-us/windows/win32/api/spellcheck/nf-spellcheck-ispellcheckerfactory-createspellchecker).
[^checker]: Microsoft Learn, [ISpellChecker interface (spellcheck.h)](https://learn.microsoft.com/en-us/windows/win32/api/spellcheck/nn-spellcheck-ispellchecker).
[^add]: Microsoft Learn, [ISpellChecker::Add](https://learn.microsoft.com/en-us/windows/win32/api/spellcheck/nf-spellcheck-ispellchecker-add).
[^autocorrect]: Microsoft Learn, [ISpellChecker::AutoCorrect](https://learn.microsoft.com/en-us/windows/win32/api/spellcheck/nf-spellcheck-ispellchecker-autocorrect).
[^error]: Microsoft Learn, [ISpellingError interface (spellcheck.h)](https://learn.microsoft.com/en-us/windows/win32/api/spellcheck/nn-spellcheck-ispellingerror).
[^action]: Microsoft Learn, [CORRECTIVE_ACTION enumeration](https://learn.microsoft.com/en-us/windows/win32/api/spellcheck/ne-spellcheck-corrective_action).
[^richedit]: Murray Sargent, [RichEdit Spell Checking, Autocorrection and Prediction](https://learn.microsoft.com/en-us/archive/blogs/murrays/richedit-spell-checking-autocorrection-and-prediction), Microsoft Learn blog archive, including the RichEdit team's replies in the comment thread on `GCMF_SPELLING` and `GetContextMenu`.
[^switches]: The Windows Club, [Enable or disable Autocorrect and Highlight Misspelled words settings in Windows 11](https://www.thewindowsclub.com/autocorrect-and-highlight-misspelled-words-settings); HTMD Blog, [Enable/Disable Autocorrect Misspelled Words](https://www.anoopcnair.com/autocorrect-misspelled-words-on-windows-11/). Community sources; the values are not documented by Microsoft. On Windows 11 build 26300 with autocorrect switched off, the values they name under `Control Panel\International` did not exist, and `EnableAutocorrection` under `TabletTip\1.7` was 0.

Further reading: Visual Studio Magazine, [Using the C++ Spell Checking API](https://visualstudiomagazine.com/articles/2013/08/01/using-the-c-spell-checking-api.aspx).
