<script setup>
defineProps({
  label: String
})

const value = defineModel('value', { type: Number, required: true })

function add(i) {
  value.value += i
}
</script>

<template>
  <div class="counter">
    <span class="label">{{ label }}: {{ value }}</span>
    <button @click="add(1)">+</button>
    <button @click="add(-1)">−</button>
  </div>
</template>

<style scoped>
.counter {
  display: flex;
  gap: 12px;
  align-items: center;
  padding: 16px;
  background: #f5f5f5;
  border-radius: 8px;
}

.counter .label {
  font-weight: 500;
  min-width: 100px;
}

.counter button {
  padding: 8px 16px;
  font-size: 1rem;
  cursor: pointer;
  border: none;
  border-radius: 4px;
  background: #1a73e8;
  color: white;
  transition: background 0.2s;
}

.counter button:hover {
  background: #1557b0;
}
</style>
