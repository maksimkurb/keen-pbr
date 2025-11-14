import { InputHTMLAttributes, forwardRef } from 'react';

interface CheckboxProps extends Omit<InputHTMLAttributes<HTMLInputElement>, 'type'> {
  label: string;
}

export const Checkbox = forwardRef<HTMLInputElement, CheckboxProps>(
  ({ label, className = '', id, ...props }, ref) => {
    const checkboxId = id || `checkbox-${Math.random().toString(36).substr(2, 9)}`;

    return (
      <div className="flex items-center">
        <input
          id={checkboxId}
          ref={ref}
          type="checkbox"
          className={`
            h-4 w-4 text-blue-600 rounded
            focus:ring-blue-500 border-gray-300
            ${className}
          `.trim()}
          {...props}
        />
        <label
          htmlFor={checkboxId}
          className="ml-2 block text-sm text-gray-700"
        >
          {label}
        </label>
      </div>
    );
  }
);

Checkbox.displayName = 'Checkbox';
