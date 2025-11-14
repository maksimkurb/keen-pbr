interface ToggleButtonGroupOption<T> {
  value: T;
  label: string;
}

interface ToggleButtonGroupProps<T> {
  options: ToggleButtonGroupOption<T>[];
  value: T;
  onChange: (value: T) => void;
  className?: string;
}

export function ToggleButtonGroup<T extends string>({
  options,
  value,
  onChange,
  className = '',
}: ToggleButtonGroupProps<T>) {
  return (
    <div className={`inline-flex rounded-md shadow-sm ${className}`} role="group">
      {options.map((option, index) => (
        <button
          key={option.value}
          type="button"
          onClick={() => onChange(option.value)}
          className={`px-4 py-2 text-sm font-medium border ${
            index === 0 ? 'rounded-l-lg' : ''
          } ${
            index === options.length - 1 ? 'rounded-r-lg' : ''
          } ${
            value === option.value
              ? 'bg-blue-600 text-white border-blue-600'
              : 'bg-white text-gray-700 border-gray-300 hover:bg-gray-50'
          }`}
        >
          {option.label}
        </button>
      ))}
    </div>
  );
}
