<script setup>
import { ref } from 'vue'

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

const rows = ref([])
const selected = ref(null)

function create1000() {
  rows.value = buildData(1000)
}

function updateRows() {
  rows.value = rows.value.map((row) => 
    ({ ...row, label: row.label + ' !!!' })
  )
}

function clear() {
  rows.value = []
}

function swapRows() {
  if (rows.value.length < 999) return
  const next = [...rows.value]
  const temp = next[1]
  next[1] = next[998]
  next[998] = temp
  rows.value = next
}

function selectRow(id) {
  selected.value = id
}

function removeRow(id) {
  rows.value = rows.value.filter(row => row.id !== id)
}
</script>

<template>
  <div class="container">
    <h1>Vue Rows Benchmark</h1>
    <div class="controls">
      <button id="create1000" @click="create1000">Create 1,000 rows</button>
      <button id="update" @click="updateRows">Update 1,000 rows</button>
      <button id="swap" @click="swapRows">Swap rows</button>
      <button id="clear" @click="clear">Clear</button>
    </div>
    <ul class="row-list">
      <li
        v-for="row in rows"
        :key="row.id"
        :class="['row', { selected: selected === row.id }]"
      >
        <span class="row-id">{{ row.id }}</span>
        <span class="row-label" @click="selectRow(row.id)">{{ row.label }}</span>
        <span class="row-actions">
          <button @click="removeRow(row.id)">×</button>
        </span>
      </li>
    </ul>
  </div>
</template>
