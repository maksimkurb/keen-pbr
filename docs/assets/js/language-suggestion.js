document.addEventListener("DOMContentLoaded", () => {
  const suggestion = document.querySelector("[data-language-suggestion]");
  const currentLanguage = document.documentElement.lang.toLowerCase();
  const normalizeLanguage = (language) => language.toLowerCase().split("-")[0];
  const options = Array.from(
    suggestion?.querySelectorAll("[data-language-suggestion-option]") ?? []
  );
  const browserLanguages = navigator.languages?.length
    ? navigator.languages
    : [navigator.language];
  const preferredLanguage = normalizeLanguage(browserLanguages[0] ?? "");
  const selectedOption = options.find(
    (option) => normalizeLanguage(option.dataset.language) === preferredLanguage
  );

  if (!suggestion || !selectedOption || normalizeLanguage(currentLanguage) === preferredLanguage) {
    return;
  }

  const dismissedKey = `keen-pbr-language-suggestion-dismissed-${selectedOption.dataset.language}`;

  try {
    if (localStorage.getItem(dismissedKey)) {
      return;
    }
  } catch {
    // The suggestion can still be shown when storage is unavailable.
  }

  suggestion.querySelector("[data-language-suggestion-title]").textContent = selectedOption.dataset.languageTitle;
  suggestion.querySelector("[data-language-suggestion-text]").textContent = selectedOption.dataset.languageText;
  selectedOption.textContent = selectedOption.dataset.languageSwitch;
  suggestion.querySelector("[data-language-suggestion-dismiss]").setAttribute(
    "aria-label",
    selectedOption.dataset.languageDismiss
  );
  selectedOption.hidden = false;
  suggestion.hidden = false;
  requestAnimationFrame(() => suggestion.classList.add("is-visible"));
  suggestion.querySelector("[data-language-suggestion-dismiss]")?.addEventListener("click", () => {
    suggestion.classList.remove("is-visible");
    suggestion.hidden = true;
    try {
      localStorage.setItem(dismissedKey, "true");
    } catch {
      // The suggestion can still be dismissed when storage is unavailable.
    }
  });
});
