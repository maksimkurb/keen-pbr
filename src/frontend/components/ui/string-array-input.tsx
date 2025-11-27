import { Plus, Trash2 } from 'lucide-react';
import { Button } from '@/components/ui/button';
import {
  InputGroup,
  InputGroupAddon,
  InputGroupButton,
  InputGroupInput,
} from '@/components/ui/input-group';

interface StringArrayInputProps {
  value: string[];
  onChange: (value: string[]) => void;
  placeholder?: string;
  disabled?: boolean;
  minItems?: number;
  addButtonLabel?: string;
}

export function StringArrayInput({
  value,
  onChange,
  placeholder,
  disabled = false,
  minItems = 1,
  addButtonLabel = 'Add',
}: StringArrayInputProps) {
  const addItem = () => {
    onChange([...value, '']);
  };

  const updateItem = (index: number, newValue: string) => {
    onChange(value.map((item, i) => i === index ? newValue : item));
  };

  const removeItem = (index: number) => {
    onChange(value.filter((_, i) => i !== index));
  };

  return (
    <div className="space-y-2 border rounded-md p-4 max-w-lg">
      {value.map((item, index) => (
        <InputGroup key={index}>
          <InputGroupInput
            value={item}
            onChange={(e) => updateItem(index, e.target.value)}
            placeholder={placeholder}
            disabled={disabled}
          />
          <InputGroupAddon align="inline-end">
            <InputGroupButton
              size="icon-xs"
              onClick={() => removeItem(index)}
              disabled={disabled || value.length <= minItems}
              aria-label="Remove"
              title="Remove"
              className="text-destructive hover:text-destructive"
            >
              <Trash2 />
            </InputGroupButton>
          </InputGroupAddon>
        </InputGroup>
      ))}
      <Button
        type="button"
        variant="outline"
        size="sm"
        onClick={addItem}
        disabled={disabled}
      >
        <Plus className="mr-2 h-4 w-4" />
        {addButtonLabel}
      </Button>
    </div>
  );
}
