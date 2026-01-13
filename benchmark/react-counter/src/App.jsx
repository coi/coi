import { useState, useEffect } from 'react'
import Counter from './Counter'
import './App.css'

function App() {
  const [score, setScore] = useState(0)

  const handleChange = (delta) => {
    setScore(prev => prev + delta)
  }

  return (
    <div className="app">
      <h1>Score: {score}</h1>
      <Counter 
        label="Player" 
        value={score} 
        onChange={handleChange} 
      />
      {score >= 10 && (
        <p className="win">🎉 You win!</p>
      )}
    </div>
  )
}

export default App
