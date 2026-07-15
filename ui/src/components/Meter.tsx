interface MeterProps {
  label: string;
  value: number;
  suffix?: string;
  minimum?: number;
  maximum?: number;
}

export function Meter({
  label,
  value,
  suffix = " dB",
  minimum = -60,
  maximum = 0,
}: MeterProps) {
  const normalized = Math.max(0, Math.min(1, (value - minimum) / (maximum - minimum)));

  return (
    <div className="meter">
      <div className="meter__header">
        <span>{label}</span>
        <strong>
          {Number.isFinite(value) ? value.toFixed(1) : "—"}
          {suffix}
        </strong>
      </div>
      <div className="meter__track" aria-label={label}>
        <span style={{ width: `${normalized * 100}%` }} />
      </div>
    </div>
  );
}
