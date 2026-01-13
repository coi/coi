<script setup>
import { ref, onMounted } from 'vue'
import Counter from './components/Counter.vue'

const score = ref(0)
</script>

<template>
  <div class="app">
    <h1>Score: {{ score }}</h1>
    <Counter 
      label="Player" 
      v-model="score" 
    />
    <p v-if="score >= 10" class="win">🎉 You win!</p>
  </div>
</template>

<style scoped>
.app {
  padding: 24px;
  max-width: 400px;
  margin: 0 auto;
}

h1 {
  color: #1a73e8;
  margin-bottom: 24px;
}

.win {
  color: #34a853;
  font-weight: bold;
  font-size: 1.25rem;
  margin-top: 16px;
  padding: 12px;
  background: #e8f5e9;
  border-radius: 8px;
}

.benchmark-container {
    margin-top: 20px;
    border-top: 1px solid #ccc;
}
</style>
