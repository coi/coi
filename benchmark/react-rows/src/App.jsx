import { useState, useCallback, useRef } from 'react'

const adjectives = ['pretty', 'large', 'big', 'small', 'tall', 'short', 'long', 'handsome', 'plain', 'quaint', 'clean', 'elegant', 'easy', 'angry', 'crazy', 'helpful', 'mushy', 'odd', 'unsightly', 'adorable', 'important', 'inexpensive', 'cheap', 'expensive', 'fancy']
const colours = ['red', 'yellow', 'blue', 'green', 'pink', 'brown', 'purple', 'brown', 'white', 'black', 'orange']
const nouns = ['table', 'chair', 'house', 'bbq', 'desk', 'car', 'pony', 'cookie', 'sandwich', 'burger', 'pizza', 'mouse', 'keyboard']

let nextId = 1

function buildData(count) {
  const data = []
  for (let i = 0; i < count; i++) {
    data.push({
      id: nextId++,
      label: `${adjectives[Math.floor(Math.random() * adjectives.length)]} ${colours[Math.floor(Math.random() * colours.length)]} ${nouns[Math.floor(Math.random() * nouns.length)]}`
    })
  }
  return data
}

function Row({ item, isSelected, onSelect, onRemove }) {
  return (
    <li className={`row ${isSelected ? 'selected' : ''}`}>
      <span className="row-id">{item.id}</span>
      <span className="row-label" onClick={() => onSelect(item.id)}>{item.label}</span>
      <span className="row-actions">
        <button onClick={() => onRemove(item.id)}>×</button>
      </span>
    </li>
  )
}

function App() {
  const [rows, setRows] = useState([])
  const [selected, setSelected] = useState(null)

  const create1000 = useCallback(() => {
    setRows(buildData(1000))
  }, [])

  const updateRows = useCallback(() => {
    setRows(prev => prev.map((row) => 
      ({ ...row, label: row.label + ' !!!' })
    ))
  }, [])

  const clear = useCallback(() => {
    setRows([])
  }, [])

  const swapRows = useCallback(() => {
    if (rows.length < 999) return
    setRows(prev => {
      const next = [...prev]
      const temp = next[1]
      next[1] = next[998]
      next[998] = temp
      return next
    })
  }, [rows.length])

  const selectRow = useCallback((id) => {
    setSelected(id)
  }, [])

  const removeRow = useCallback((id) => {
    setRows(prev => prev.filter(row => row.id !== id))
  }, [])

  return (
    <div className="container">
      <h1>React Rows Benchmark</h1>
      <div className="controls">
        <button id="create1000" onClick={create1000}>Create 1,000 rows</button>
        <button id="update" onClick={updateRows}>Update 1,000 rows</button>
        <button id="swap" onClick={swapRows}>Swap rows</button>
        <button id="clear" onClick={clear}>Clear</button>
      </div>
      <ul className="row-list">
        {rows.map(item => (
          <Row
            key={item.id}
            item={item}
            isSelected={selected === item.id}
            onSelect={selectRow}
            onRemove={removeRow}
          />
        ))}
      </ul>
    </div>
  )
}

export default App
