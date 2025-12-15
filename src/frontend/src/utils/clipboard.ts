/**
 * Copy text to clipboard with fallback support for non-secure contexts
 * Works in both HTTP and HTTPS environments
 *
 * @param text - The text to copy to clipboard
 * @returns Promise that resolves when text is copied, rejects if copy fails
 */
export async function copyToClipboard(text: string): Promise<void> {
  const textArea = document.createElement('textarea');
  textArea.value = text;

  // Make it invisible and off-screen
  textArea.style.position = 'fixed';
  textArea.style.left = '-999999px';
  textArea.style.top = '-999999px';

  document.body.appendChild(textArea);

  try {
    // Select the text
    textArea.select();
    textArea.setSelectionRange(0, text.length);

    // Try to copy
    const successful = document.execCommand('copy');
    if (!successful) {
      throw new Error('execCommand copy failed');
    }
  } finally {
    // Clean up
    document.body.removeChild(textArea);
  }
}
