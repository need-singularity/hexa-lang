# `nn`

_Hexa Neural Network Standard Library_

**Source:** [`stdlib/nn.hexa`](../../stdlib/nn.hexa)  

## Overview

Hexa Neural Network Standard Library
Pure Hexa implementations of NN primitives + high-level layers

Primitives work on float arrays (lists). Tensor builtins are
immutable, so element-wise ops use mutable arrays internally.

── Activation: ReLU ─────────────────────────────────────
relu(x) = max(0, x)  element-wise

## Functions

| Function | Returns |
|---|---|
| [`relu`](#fn-relu) | `_inferred_` |
| [`sigmoid`](#fn-sigmoid) | `_inferred_` |
| [`tanh_activation`](#fn-tanh-activation) | `_inferred_` |
| [`softmax`](#fn-softmax) | `_inferred_` |
| [`gelu`](#fn-gelu) | `_inferred_` |
| [`layer_norm`](#fn-layer-norm) | `_inferred_` |

### <a id="fn-relu"></a>`fn relu`

```hexa
pub fn relu(x)
```

**Parameters:** `x`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-sigmoid"></a>`fn sigmoid`

```hexa
pub fn sigmoid(x)
```

**Parameters:** `x`  
**Returns:** _inferred_  

── Activation: Sigmoid ──────────────────────────────────
sigmoid(x) = 1 / (1 + exp(-x))  element-wise

### <a id="fn-tanh-activation"></a>`fn tanh_activation`

```hexa
pub fn tanh_activation(x)
```

**Parameters:** `x`  
**Returns:** _inferred_  

── Activation: Tanh ─────────────────────────────────────
tanh(x) = (exp(2x) - 1) / (exp(2x) + 1)  element-wise

### <a id="fn-softmax"></a>`fn softmax`

```hexa
pub fn softmax(x)
```

**Parameters:** `x`  
**Returns:** _inferred_  

── Activation: Softmax ──────────────────────────────────
softmax(x)_i = exp(x_i - max(x)) / sum(exp(x_j - max(x)))
Numerically stable (max subtraction prevents overflow).

### <a id="fn-gelu"></a>`fn gelu`

```hexa
pub fn gelu(x_val)
```

**Parameters:** `x_val`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-layer-norm"></a>`fn layer_norm`

```hexa
pub fn layer_norm(x, gamma, beta)
```

**Parameters:** `x, gamma, beta`  
**Returns:** _inferred_  

── Layer Normalization ──────────────────────────────────
layer_norm(x, gamma, beta) normalizes x to zero mean / unit
variance then applies affine transform: gamma * x_hat + beta.

x, gamma, beta: float arrays of equal length

---

← [Back to stdlib index](README.md)
