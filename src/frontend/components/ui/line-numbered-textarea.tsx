import { forwardRef, useEffect, useRef, useState } from 'react';
import { cn } from '../../src/lib/utils';

export interface LineError {
  [lineNumber: number]: string[];
}

interface LineNumberedTextareaProps extends React.TextareaHTMLAttributes<HTMLTextareaElement> {
  errors?: LineError;
}

export const LineNumberedTextarea = forwardRef<HTMLTextareaElement, LineNumberedTextareaProps>(
  ({ className, errors, value, onChange, ...props }, ref) => {
    const textareaRef = useRef<HTMLTextareaElement>(null);
    const lineNumbersRef = useRef<HTMLDivElement>(null);
    const [lineCount, setLineCount] = useState(1);

    // Combine external ref with internal ref
    useEffect(() => {
      if (ref) {
        if (typeof ref === 'function') {
          ref(textareaRef.current);
        } else {
          ref.current = textareaRef.current;
        }
      }
    }, [ref]);

    // Update line count when value changes
    useEffect(() => {
      const text = (value as string) || '';
      const lines = text.split('\n').length;
      setLineCount(lines);
    }, [value]);

    // Sync scroll between textarea and line numbers
    const handleScroll = () => {
      if (textareaRef.current && lineNumbersRef.current) {
        lineNumbersRef.current.scrollTop = textareaRef.current.scrollTop;
      }
    };

    const hasErrors = errors && Object.keys(errors).length > 0;

    return (
      <div className="relative flex border rounded-md overflow-hidden">
        {/* Line numbers */}
        <div
          ref={lineNumbersRef}
          className="flex flex-col bg-muted text-muted-foreground text-sm leading-6 py-2 px-2 overflow-hidden select-none"
          style={{
            minWidth: '3rem',
            textAlign: 'right',
            fontFamily: 'monospace',
          }}
        >
          {Array.from({ length: lineCount }, (_, i) => {
            const lineNum = i + 1;
            const hasError = errors && errors[lineNum];
            return (
              <div
                key={lineNum}
                className={cn(
                  "leading-6",
                  hasError && "text-red-600 dark:text-red-400 font-semibold"
                )}
                title={hasError ? errors[lineNum].join(', ') : undefined}
              >
                {lineNum}
              </div>
            );
          })}
        </div>

        {/* Textarea */}
        <textarea
          ref={textareaRef}
          value={value}
          onChange={onChange}
          onScroll={handleScroll}
          className={cn(
            "flex-1 min-h-[200px] resize-y bg-background text-sm p-2 focus:outline-none focus:ring-2 focus:ring-ring focus:ring-offset-2 leading-6 border-0",
            hasErrors && "bg-red-50 dark:bg-red-950/20",
            className
          )}
          style={{
            fontFamily: 'monospace',
            lineHeight: '1.5rem',
          }}
          {...props}
        />
      </div>
    );
  }
);

LineNumberedTextarea.displayName = 'LineNumberedTextarea';
