<script>
  import Row from './Row.svelte'

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

  let rows = $state([])
  let selected = $state(null)

  function create1000() {
    rows = buildData(1000)
  }

  function updateRows() {
    for (let i = 0; i < rows.length; i++) {
      rows[i].label += ' !!!'
    }
  }

  function clear() {
    rows = []
  }

  function swapRows() {
    if (rows.length < 999) return
    const temp = rows[1]
    rows[1] = rows[998]
    rows[998] = temp
  }

  function selectRow(id) {
    selected = id
  }

  function removeRow(id) {
    rows = rows.filter(row => row.id !== id)
  }
</script>

<div class="container">
  <h1>Svelte Rows Benchmark</h1>
  <div class="controls">
    <button id="create1000" onclick={create1000}>Create 1,000 rows</button>
    <button id="update" onclick={updateRows}>Update 1,000 rows</button>
    <button id="swap" onclick={swapRows}>Swap rows</button>
    <button id="clear" onclick={clear}>Clear</button>
  </div>
  <ul class="row-list">
    {#each rows as item (item.id)}
      <Row 
        {item}
        isSelected={selected === item.id}
        onSelect={selectRow}
        onRemove={removeRow}
      />
    {/each}
  </ul>
</div>
