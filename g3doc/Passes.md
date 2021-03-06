# MLIR Passes

This document describes the available MLIR passes and their contracts.

[TOC]

## Affine control lowering (`-lower-affine`)

Convert operations related to affine control into a graph of blocks using
operations from the standard dialect.

Loop statements are converted to a subgraph of blocks (initialization, condition
checking, subgraph of body blocks) with loop induction variable being passed as
the block argument of the condition checking block. Conditional statements are
converted to a subgraph of blocks (chain of condition checking with
short-circuit logic, subgraphs of 'then' and 'else' body blocks). `affine.apply`
operations are converted into sequences of primitive arithmetic operations that
have the same effect, using operands of the `index` type. Consequently, named
maps and sets may be removed from the module.

For example, `%r = affine.apply (d0, d1)[s0] -> (d0 + 2*d1 + s0)(%d0, %d1)[%s0]`
can be converted into:

```mlir
%d0 = <...>
%d1 = <...>
%s0 = <...>
%0 = constant 2 : index
%1 = muli %0, %d1
%2 = addi %d0, %1
%r = addi %2, %s0
```

### Input invariant

-   no `Tensor` types;

These restrictions may be lifted in the future.

### Output IR

Functions with `affine.for` and `affine.if` operations eliminated. These
functions may contain operations from the Standard dialect in addition to those
already present before the pass.

### Invariants

-   Functions without a body are not modified.
-   The semantics of the other functions is preserved.
-   Individual operations other than those mentioned above are not modified if
    they do not depend on the loop iterator value or on the result of
    `affine.apply`.

## Conversion from Standard to LLVM IR dialect (`-convert-to-llvmir`)

Convert standard operations into the LLVM IR dialect operations.

### Input invariant

-   operations including: arithmetic on integers and floats, constants, direct
    calls, returns and branches;
-   no `tensor` types;
-   all `vector` are one-dimensional;
-   all blocks are reachable by following the successors of the first basic
    block;

If other operations are present and their results are required by the LLVM IR
dialect operations, the pass will fail.  Any LLVM IR operations or types already
present in the IR will be kept as is.

### Output IR

Functions converted to LLVM IR. Function arguments types are converted
one-to-one. Function results are converted one-to-one and, in case more than 1
value is returned, packed into an LLVM IR struct type. Function calls and
returns are updated accordingly. Block argument types are updated to use LLVM IR
types.

## DMA generation (`-dma-generate`)

Replaces all loads and stores on memref's living in 'slowMemorySpace' by
introducing DMA operations (strided DMA if necessary) to transfer data to/from
`fastMemorySpace` and rewriting the original load's/store's to instead
load/store from the allocated fast memory buffers. Additional options specify
the identifier corresponding to the fast memory space and the amount of fast
memory space available. The pass traverses through the nesting structure,
recursing to inner levels if necessary to determine at what depth DMA transfers
need to be placed so that the allocated buffers fit within the memory capacity
provided. If this is not possible (for example, when the elemental type itself
is of size larger than the DMA capacity), an error with location information is
emitted. The DMA transfers are also hoisted up past all loops with respect to
which the transfers are invariant.

Input

```mlir
func @loop_nest_tiled() -> memref<256x1024xf32> {
  %0 = alloc() : memref<256x1024xf32>
  affine.for %i0 = 0 to 256 step 32 {
    affine.for %i1 = 0 to 1024 step 32 {
      affine.for %i2 = (d0) -> (d0)(%i0) to (d0) -> (d0 + 32)(%i0) {
        affine.for %i3 = (d0) -> (d0)(%i1) to (d0) -> (d0 + 32)(%i1) {
          %1 = load %0[%i2, %i3] : memref<256x1024xf32>
        }
      }
    }
  }
  return %0 : memref<256x1024xf32>
}
```

Output

```mlir
#map1 = (d0) -> (d0)
#map2 = (d0) -> (d0 + 32)
#map3 = (d0, d1) -> (-d0 + d1)
func @loop_nest_tiled() -> memref<256x1024xf32> {
  %c1024 = constant 1024 : index
  %c32 = constant 32 : index
  %c0 = constant 0 : index
  %0 = alloc() : memref<256x1024xf32>
  affine.for %i0 = 0 to 256 step 32 {
    affine.for %i1 = 0 to 1024 step 32 {
      %1 = affine.apply #map1(%i0)
      %2 = affine.apply #map1(%i1)
      %3 = alloc() : memref<32x32xf32, 1>
      %4 = alloc() : memref<1xi32>
      dma_start %0[%1, %2], %3[%c0, %c0], %c1024, %4[%c0], %c1024, %c32 : memref<256x1024xf32>, memref<32x32xf32, 1>, memref<1xi32>
      dma_wait %4[%c0], %c1024 : memref<1xi32>
      affine.for %i2 = #map1(%i0) to #map2(%i0) {
        affine.for %i3 = #map1(%i1) to #map2(%i1) {
          %5 = affine.apply #map3(%i0, %i2)
          %6 = affine.apply #map3(%i1, %i3)
          %7 = load %3[%5, %6] : memref<32x32xf32, 1>
        }
      }
      dealloc %4 : memref<1xi32>
      dealloc %3 : memref<32x32xf32, 1>
    }
  }
  return %0 : memref<256x1024xf32>
}
```

## Loop tiling (`-loop-tile`)

Performs tiling or blocking of loop nests. It currently works on perfect loop
nests.

## Loop unroll (`-loop-unroll`)

This pass implements loop unrolling. It is able to unroll loops with arbitrary
bounds, and generate a cleanup loop when necessary.

## Loop unroll and jam (`-loop-unroll-jam`)

This pass implements unroll and jam for loops. It works on both perfect or
imperfect loop nests.

## Loop fusion (`-loop-fusion`)

Performs fusion of loop nests using a slicing-based approach. The fused loop
nests, when possible, are rewritten to access significantly smaller local
buffers instead of the original memref's, and the latter are often
either completely optimized away or contracted. This transformation leads to
enhanced locality and lower memory footprint through the elimination or
contraction of temporaries / intermediate memref's. These benefits are sometimes
achieved at the expense of redundant computation through a cost model that
evaluates available choices such as the depth at which a source slice should be
materialized in the designation slice.

## Memref bound checking (`-memref-bound-check`)

Checks all load's and store's on memref's for out of bound accesses, and reports
any out of bound accesses (both overrun and underrun) with location information.

```mlir
test/Transforms/memref-bound-check.mlir:19:13: error: 'load' op memref out of upper bound access along dimension #2
      %x  = load %A[%idx0, %idx1] : memref<9 x 9 x i32>
            ^
test/Transforms/memref-bound-check.mlir:19:13: error: 'load' op memref out of lower bound access along dimension #2
      %x  = load %A[%idx0, %idx1] : memref<9 x 9 x i32>
            ^
```

## Memref dataflow optimization (`-memref-dataflow-opt`)

This pass performs store to load forwarding for memref's to eliminate memory
accesses and potentially the entire memref if all its accesses are forwarded.

Input

```mlir
func @store_load_affine_apply() -> memref<10x10xf32> {
  %cf7 = constant 7.0 : f32
  %m = alloc() : memref<10x10xf32>
  affine.for %i0 = 0 to 10 {
    affine.for %i1 = 0 to 10 {
      %t0 = affine.apply (d0, d1) -> (d1 + 1)(%i0, %i1)
      %t1 = affine.apply (d0, d1) -> (d0)(%i0, %i1)
      %idx0 = affine.apply (d0, d1) -> (d1) (%t0, %t1)
      %idx1 = affine.apply (d0, d1) -> (d0 - 1) (%t0, %t1)
      store %cf7, %m[%idx0, %idx1] : memref<10x10xf32>
      %v0 = load %m[%i0, %i1] : memref<10x10xf32>
      %v1 = addf %v0, %v0 : f32
    }
  }
  return %m : memref<10x10xf32>
}
```

Output

```mlir
#map1 = (d0, d1) -> (d1)
#map2 = (d0, d1) -> (d0 - 1)
func @store_load_affine_apply() -> memref<10x10xf32> {
  %cst = constant 7.000000e+00 : f32
  %0 = alloc() : memref<10x10xf32>
  affine.for %i0 = 0 to 10 {
    affine.for %i1 = 0 to 10 {
      %3 = affine.apply #map1(%1, %2)
      %4 = affine.apply #map2(%1, %2)
      store %cst, %0[%3, %4] : memref<10x10xf32>
      %5 = addf %cst, %cst : f32
    }
  }
  return %0 : memref<10x10xf32>
}
```

## Memref dependence analysis (`-memref-dependence-check`)

This pass performs dependence analysis to determine dependences between pairs of
memory operations (load's and store's) on memref's. Dependence analysis exploits
polyhedral information available (affine maps, expressions, and affine.apply
operations) to precisely represent dependences using affine constraints, while
also computing dependence vectors from them, where each component of the
dependence vector provides a lower and an upper bound on the dependence distance
along the corresponding dimension.

```mlir
test/Transforms/memref-dataflow-opt.mlir:232:7: note: dependence from 2 to 1 at depth 1 = ([1, 1], [-inf, +inf])
      store %cf9, %m[%idx] : memref<10xf32>
```

## Pipeline data transfer (`-pipeline-data-transfer`)

This pass performs a transformation to overlap non-blocking DMA operations in a
loop with computations through double buffering. This is achieved by advancing
dma_start operations with respect to other operations.

Input

```mlir
  %0 = alloc() : memref<256xf32>
  %1 = alloc() : memref<32xf32, 1>
  %2 = alloc() : memref<1xf32>
  %c0 = constant 0 : index
  %c128 = constant 128 : index
  affine.for %i0 = 0 to 8 {
    dma_start %0[%i0], %1[%i0], %c128, %2[%c0] : memref<256xf32>, memref<32xf32, 1>, memref<1xf32>
    dma_wait %2[%c0], %c128 : memref<1xf32>
    %3 = load %1[%i0] : memref<32xf32, 1>
    %4 = "compute"(%3) : (f32) -> f32
    store %4, %1[%i0] : memref<32xf32, 1>
  }
```

Output

```mlir
#map2 = (d0) -> (d0 mod 2)
#map3 = (d0) -> (d0 - 1)
#map4 = (d0) -> ((d0 - 1) mod 2)

  %c128 = constant 128 : index
  %c0 = constant 0 : index
  %c7 = constant 7 : index
  %c1 = constant 1 : index
  %0 = alloc() : memref<256xf32>
  %1 = alloc() : memref<2x32xf32, 1>
  %2 = alloc() : memref<2x1xf32>
  dma_start %0[%c0], %1[%c0, %c0], %c128, %2[%c0, %c0] : memref<256xf32>, memref<2x32xf32, 1>, memref<2x1xf32>
  affine.for %i0 = 1 to 8 {
    %3 = affine.apply #map2(%i0)
    %4 = affine.apply #map2(%i0)
    dma_start %0[%i0], %1[%3, %i0], %c128, %2[%4, %c0] : memref<256xf32>, memref<2x32xf32, 1>, memref<2x1xf32>
    %5 = affine.apply #map3(%i0)
    %6 = affine.apply #map4(%i0)
    %7 = affine.apply #map4(%i0)
    dma_wait %2[%6, %c0], %c128 : memref<2x1xf32>
    %8 = load %1[%7, %5] : memref<2x32xf32, 1>
    %9 = "compute"(%8) : (f32) -> f32
    store %9, %1[%7, %5] : memref<2x32xf32, 1>
  }
  dma_wait %2[%c1, %c0], %c128 : memref<2x1xf32>
  %10 = load %1[%c1, %c7] : memref<2x32xf32, 1>
  %11 = "compute"(%10) : (f32) -> f32
  store %11, %1[%c1, %c7] : memref<2x32xf32, 1>
  dealloc %2 : memref<2x1xf32>
  dealloc %1 : memref<2x32xf32, 1>
```
