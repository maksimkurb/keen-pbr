document.addEventListener("DOMContentLoaded", () => {
  document.querySelectorAll("[data-yoomoney-donation-form]").forEach((form) => {
    const amount = form.querySelector('input[name="sum"]');
    const amountError = form.querySelector(`#${amount.id}-error`);
    const supporterName = form.querySelector("[data-supporter-name]");
    const consent = form.querySelector("[data-publication-consent]");
    const consentError = form.querySelector(`#${consent.getAttribute("aria-describedby").split(" ")[1]}`);
    const paymentLabel = form.querySelector("[data-yoomoney-label]");
    const incrementButtons = form.querySelectorAll("[data-amount-increment]");
    const submit = form.querySelector('button[type="submit"]');

    const updateIncrementButtons = () => {
      const currentAmount = Number.isNaN(amount.valueAsNumber)
        ? 0
        : amount.valueAsNumber;
      const max = Number(amount.max);

      incrementButtons.forEach((button) => {
        const increment = Number(button.dataset.amountIncrement);
        button.disabled = currentAmount + increment > max;
      });
    };

    const validateAmount = (showMessage = true) => {
      const value = amount.valueAsNumber;
      const min = Number(amount.min);
      const max = Number(amount.max);
      let message = "";

      if (amount.value.trim() === "" || Number.isNaN(value)) {
        message = form.dataset.amountRequired;
      } else if (!Number.isInteger(value)) {
        message = form.dataset.amountInteger;
      } else if (value < min) {
        message = form.dataset.amountMin;
      } else if (value > max) {
        message = form.dataset.amountMax;
      }

      amount.setCustomValidity(message);
      amount.setAttribute("aria-invalid", message ? "true" : "false");
      amountError.textContent = showMessage ? message : "";
      amountError.hidden = !showMessage || !message;
      return !message;
    };

    const validateConsent = (showMessage = true) => {
      const hasSupporterName = supporterName.value.trim() !== "";
      const message = hasSupporterName && !consent.checked
        ? form.dataset.consentRequired
        : "";

      consent.disabled = !hasSupporterName;
      consent.required = hasSupporterName;
      if (!hasSupporterName) {
        consent.checked = false;
      }

      consent.setCustomValidity(message);
      consent.setAttribute("aria-invalid", message ? "true" : "false");
      consentError.textContent = showMessage ? message : "";
      consentError.hidden = !showMessage || !message;
      return !message;
    };

    const validateForm = (showMessages = true) => {
      const amountIsValid = validateAmount(showMessages);
      const consentIsValid = validateConsent(showMessages);
      submit.disabled = !amountIsValid || !consentIsValid;
      updateIncrementButtons();
      return amountIsValid && consentIsValid;
    };

    amount.addEventListener("input", () => validateForm());
    amount.addEventListener("blur", () => validateForm());
    supporterName.addEventListener("input", () => validateForm());
    consent.addEventListener("change", () => validateForm());
    incrementButtons.forEach((button) => {
      button.addEventListener("click", () => {
        const currentAmount = Number.isNaN(amount.valueAsNumber)
          ? 0
          : amount.valueAsNumber;
        amount.value = currentAmount + Number(button.dataset.amountIncrement);
        validateForm();
        amount.focus();
      });
    });

    form.addEventListener("submit", (event) => {
      if (!validateForm()) {
        event.preventDefault();
        const invalidField = form.querySelector(":invalid");
        invalidField?.focus();
        return;
      }

      const publicName = supporterName.value.trim();
      paymentLabel.value = publicName
        ? `keen-pbr-public-v1:${publicName}`
        : "keen-pbr";

      const fieldValues = Array.from(new FormData(form).entries())
        .map(([name, value]) => `${name}: ${value}`)
        .join("\n");
      // if (!window.confirm(`${form.dataset.confirmMessage}\n\n${fieldValues}`)) {
      //   event.preventDefault();
      //   return;
      // }

      submit.disabled = true;
      submit.textContent = form.dataset.submittingLabel;
    });

    validateForm(false);
  });
});
