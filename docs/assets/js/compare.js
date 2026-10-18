(() => {
  if (window.keenPbrCompareInit) {
    return;
  }
  window.keenPbrCompareInit = true;

  const clamp = (value) => Math.min(100, Math.max(0, value));

  const init = (root) => {
    if (root.classList.contains("compare-ready")) {
      return;
    }
    const stage = root.querySelector(".compare-stage");
    const handle = root.querySelector(".compare-handle");
    if (!stage || !handle) {
      return;
    }

    const initial = parseFloat(handle.getAttribute("aria-valuenow"));
    let pos = clamp(Number.isFinite(initial) ? initial : 30);
    const setPos = (value) => {
      pos = clamp(value);
      root.style.setProperty("--pos", `${pos}%`);
      handle.setAttribute("aria-valuenow", String(Math.round(pos)));
    };
    const fromEvent = (event) => {
      const rect = stage.getBoundingClientRect();
      if (rect.width > 0) {
        setPos(((event.clientX - rect.left) / rect.width) * 100);
      }
    };

    let dragging = false;
    stage.addEventListener("pointerdown", (event) => {
      if (event.pointerType === "mouse" && event.button !== 0) {
        return;
      }
      dragging = true;
      stage.setPointerCapture(event.pointerId);
      root.classList.add("compare-dragging");
      fromEvent(event);
      handle.focus({ preventScroll: true });
      event.preventDefault();
    });
    stage.addEventListener("pointermove", (event) => {
      if (dragging) {
        fromEvent(event);
      }
    });
    const stop = (event) => {
      if (!dragging) {
        return;
      }
      dragging = false;
      root.classList.remove("compare-dragging");
      if (stage.hasPointerCapture(event.pointerId)) {
        stage.releasePointerCapture(event.pointerId);
      }
    };
    stage.addEventListener("pointerup", stop);
    stage.addEventListener("pointercancel", stop);

    handle.addEventListener("keydown", (event) => {
      const step = event.shiftKey ? 10 : 2;
      switch (event.key) {
        case "ArrowLeft":
        case "ArrowDown":
          setPos(pos - step);
          break;
        case "ArrowRight":
        case "ArrowUp":
          setPos(pos + step);
          break;
        case "Home":
          setPos(0);
          break;
        case "End":
          setPos(100);
          break;
        default:
          return;
      }
      event.preventDefault();
    });

    setPos(pos);
    root.classList.add("compare-ready");
  };

  const initAll = () => document.querySelectorAll(".compare").forEach(init);
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initAll);
  } else {
    initAll();
  }
})();
