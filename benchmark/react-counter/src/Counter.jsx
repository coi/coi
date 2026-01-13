import './Counter.css'

function Counter({ label, value, onChange }) {
  return (
    <div className="counter">
      <span className="label">{label}: {value}</span>
      <button onClick={() => onChange(1)}>+</button>
      <button onClick={() => onChange(-1)}>−</button>
    </div>
  )
}

export default Counter
