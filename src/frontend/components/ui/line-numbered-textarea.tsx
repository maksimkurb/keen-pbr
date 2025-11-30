import { forwardRef, useEffect, useRef, useState } from 'react';
import { cn } from '../../src/lib/utils';
import {
  Tooltip,
  TooltipContent,
  TooltipProvider,
  TooltipTrigger,
} from './tooltip';

export interface LineError {
  [lineNumber: number]: string;
}

interface LineNumberedTextareaProps
  extends React.TextareaHTMLAttributes<HTMLTextAreaElement> {
  errors?: LineError;
  height?: string | number;
}

export const LineNumberedTextarea = forwardRef<
  HTMLTextAreaElement,
  LineNumberedTextareaProps
>(({ className, errors, value, onChange, height = '400px', ...props }, ref) => {
  const textareaRef = useRef<HTMLTextAreaElement>(null);
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
  const heightValue = typeof height === 'number' ? `${height}px` : height;

  return (
    <TooltipProvider>
      <div
        className="relative border rounded-md overflow-visible"
        style={{ height: heightValue }}
      >
        <div className="flex h-full overflow-hidden">
          {/* Line numbers */}
          <div
            ref={lineNumbersRef}
            className="flex flex-col bg-muted text-muted-foreground text-sm py-2 select-none shrink-0 relative"
            style={{
              minWidth: '3rem',
              textAlign: 'right',
              fontFamily: 'monospace',
              overflowY: 'hidden',
              lineHeight: '1.5rem',
            }}
          >
            {Array.from({ length: lineCount }, (_, i) => {
              const lineNum = i + 1;
              const errorMessage = errors?.[lineNum];

              const lineNumberElement = (
                <div
                  className={`px-2${errorMessage ? ' text-destructive-foreground bg-destructive' : ''}`}
                >
                  {lineNum}
                </div>
              );

              if (errorMessage) {
                return (
                  <Tooltip key={lineNum}>
                    <TooltipTrigger asChild>{lineNumberElement}</TooltipTrigger>
                    <TooltipContent side="right">
                      <div className="text-sm">{errorMessage}</div>
                    </TooltipContent>
                  </Tooltip>
                );
              }

              return <div key={lineNum}>{lineNumberElement}</div>;
            })}
          </div>

          {/* Textarea */}
          <textarea
            ref={textareaRef}
            value={value}
            onChange={onChange}
            onScroll={handleScroll}
            className={cn(
              'flex-1 resize-none bg-background text-sm p-2 focus:outline-none focus:ring-2 focus:ring-ring focus:ring-offset-2 border-0',
              hasErrors && 'bg-red-50 dark:bg-red-950/20',
              className,
            )}
            style={{
              fontFamily: 'monospace',
              lineHeight: '1.5rem',
              overflowY: 'scroll',
              overflowX: 'hidden',
            }}
            {...props}
          />
        </div>
      </div>
    </TooltipProvider>
  );
});

LineNumberedTextarea.displayName = 'LineNumberedTextarea';
