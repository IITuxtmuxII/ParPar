The GF-Complete library provides a [very fast SSSE3
implementation](http://web.eecs.utk.edu/~plank/plank/papers/FAST-2013-GF.html)
for GF region multiplication, dubbed SPLIT_TABLE(16,4). This method abuses the
`pshufb` instruction to perform parallel 4-bit multiplies. For cases where the
CPU does not support SSSE3, GF-Complete falls back to a significantly slower
SPLIT_TABLE(16,8) method, which does not use SIMD, instead relying on scalar
lookups \*.  
I present a technique below, named “XOR_DEPENDS”, suitable for CPUs with SSE2
support but not SSSE3, such as the AMD K10. The technique is also a viable
alternative for SSSE3 CPUs with a slow `pshufb` instruction (i.e. Intel Atom).

\* A bit of SIMD can be used to accelerate the lookup table by a fair amount
(around 50% in my rough tests), but ultimately are still much slower than faster
techniques

 

Theory
======

For modular multiplication in *GF(2 \<pow\> w)*, each bit of the product can be
derived by taking specific bits from the multiplicand and XORing (performing
exclusive or) them.

For example, suppose we wish to multiply the 4-bit number “[a][b][c][d]” (where
*a* is the first bit, *b* the second and so on) by 2 in GF(16), with polynomial
0x13. Multiplying by 2 is simply a left shift by 1 bit, after which, we need to
keep the product in the field via modular reduction. If a is 1, we need to XOR
the polynomial after the shift, otherwise, nothing else needs to be performed.

In other words (where \^ denotes XOR and \* denotes modular multiplication),
[a][b][c][d] \* 2 equals:

-   if a = 1: [a][b][c][d][0] \^ 10011 = [b][c][d\^1][1]

-   if a = 0: [a][b][c][d][0] = [b][c][d][0]

Or more concisely:

[a][b][c][d] \* 2 = [b][c][d\^a][a]

 

We can recursively apply the above definition for any multiplier, eg:

[a][b][c][d] \* 3 = ([a][b][c][d] \* 2) + [a][b][c][d] = [b][c][d\^a][a] \^
[a][b][c][d] = [a\^b][b\^c][a\^c\^d][a\^d]

[a][b][c][d] \* 4 = ([a][b][c][d] \* 2) \* 2 = [c][a\^d][a\^b][b]

This is similar to how the BYTWO method in GF-Complete operates.

Since we’re performing modular multiplication by a constant to a region of data,
once we calculate which bits to XOR together to produce the product, we can
simply apply the same sequence of XORs to each word in the data.

 

Optimisations
=============

Shifting and selectively XORing single bits isn’t particularly fast. However,
CPUs can often operate on more than one bit at a time. If we can rearrange the
input (and output) regions, we could operate on multiple bits in parallel.

SSE supports 128 bit operations, therefore, we should rearrange our data so that
the most significant bit for 128 words are stored together, followed by the next
significant bit of these 128 words, and so on.

PAR2 uses GF(65536), so we will focus on PAR2’s case for the rest of the
document. The ideas here can be used for other GF sizes (even non powers of 2).  
For GF(65536) we operate on 16 \* 128 bit = 256 byte blocks. The algorithm
simply generates the most significant bits of 128 products by selectively XORing
various bits of the 128 muliplicands. This is repeated for all 16 bits in the
word.

JIT
---

A problem that remains is, how do we determine which specific bits of the input
need to be XORed to get the product? Whilst we can determine the bit
dependencies via the theory above, sprinkling if conditions throughout the inner
processing loop will hurt performance.

The (non-JIT) algorithm uses lookup tables to get the appropriate input bits to
use, as well as a jump table (switch statement) to handle the variable number of
XOR operations that need to be performed. Some C-like code:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
switch(xor_count[bit]) {
  case 16: dest ^= source[dependency[bit][15]];
  case 15: dest ^= source[dependency[bit][14]];
  // etc
  case  2: dest ^= source[dependency[bit][ 1]];
  case  1: dest ^= source[dependency[bit][ 0]];
}
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Whilst this performs fairly well (usually beating SPLIT_TABLE(16,8)), the lookup
operation and jump table have a fair impact on performance.

We could eliminate these overheads by writing precomputed functions for every
multiplicand - whilst this would work for small fields (such as 8 bits or less),
it’s problematic for GF(65536). See the Static Pre-generation section below for
more on this.

Alternatively, we can look to JIT to dynamically generate such functions at
runtime, which is what the JIT version of the algorithm does.

Unfortunately, this does require the OS to allocate a memory page that is both
writable and executable. It’s theoretically possible to switch between
write/execute permissions, for OSes that implement
[W\^X](https://en.wikipedia.org/wiki/W%5EX), but I expect that the syscall
overhead would be significant.

### Self-modifying Code

Unfortunately, a cost of this form of single use JIT is that it displays
properties of self-modifying code, which modern processors perform poorly with.
This is most evident with the poor performance for small amounts of data.
However, despite this, performance seems to be good for large amounts of data.

Optimal Sequences
-----------------

Assuming a 16 bit GF, for each bit of the product, we need to perform 0-15 XOR
operations. On average, there’s 8 XOR operations (this is exactly true for the
0x1100B polynomial), and considering the load/store operation, 10 instructions
to process 16 bytes of data (add one more XOR if we need to merge with the
product).

Many of these XOR sequences have common sub-sequences, and hence, we can further
optimise by avoiding these duplicate XOR operations. To illustrate, suppose we
wish to multiple the 4 bit number [a][b][c][d] by 7 in GF(16) with polynomial
0x13. The result, [w][x][y][z], can be determined by:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
w = a^b^c
x = a^b^c^d
y = b^c^d
z = a^b^d
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You may notice that there are “duplicate” XOR operations above, which can be
eliminated, reducing 9 XOR operations to 5, for example:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
w = a^b^c
x = w^d
y = x^a
z = x^c
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Finding these common sub-sequences, in an efficient manner (i.e. not make the
JIT code rather slow), is somewhat of a challenge. I’m not sure what good
strategies exist, but I’ve investigated the following heuristics, with the
average number of XOR operations performed per 256 byte block (including the XOR
into destination, for the 16 bit GF):

-   **None** (128 XORs/block): i.e. no attempt to find common sub-sequences
    results in each output needing an average of 8 XOR operations

-   **Adjacent pairs** (103.807 XORs/block): common elements between adjacent
    pairs (can be implemented as a bitwise AND of the two dependency masks) are
    extracted and calculated as a temp variable, then merged (XORed) back in at
    the end. This is relatively simple to implement, can be calculated at
    runtime very quickly, and provides a good overall speedup. The current JIT
    implementation employs this heuristic.  
    Actual implementation may cause additional MOV instructions to be added for
    the temporary sequences, if 3-operand syntax is unavailable (e.g. in SSE).

-   **Greedy pairs** (93.750 XORs/block): takes the idea from above, however an
    attempt to find an optimal ordering of pairs is made by selecting pairs with
    the most common elements first. This greedy approach may not necessarily be
    optimal (not a brute force search) but should be fairly close. The search
    may be fast enough to compute at runtime (though should invoke a fair speed
    hit), failing that, it should be possible to precompute the ordering and
    pack and store it. If storing precomputed orderings is to be done, each
    ordering can trivially fit into 8 bytes (16x 4-bit indicies), or 5 bytes
    (totaling 320KB) if a more complex packing scheme is to be used. From
    testing, this doesn’t seem to improve actual performance, possible due to
    reducing parallelism available (see below).

-   **Pair of pairs** (89.739 XORs/block?): like above, however the common
    sub-sequences themselves are searched for common elements. This means that
    outputs are calculated in groups of 4 instead of 2, and the actual JIT code
    needs 2 temp variables rather than 1. Likely far too slow to calculate at
    runtime, but if precomputed tables are used, it doesn’t consume any more
    space than the above method.

-   **Group by 4 with 2 temporaries** (90.980 XORs/block?): try to optimally
    partition the 16 output sequences into 4 groups of 4. Within each group,
    find the best 2 masks to mix in, where each mask can mix with any of the 4
    sequences. Extremely slow search as it performs some exhaustive
    permutations, yet it doesn’t seem to perform particularly well. This seems
    to suggest that partitioning is quite detrimental to finding duplicate XORs,
    which works best when applied across all sequences.

-   **GCC optimised** (97.507 XORs/block): optimised code generated by GCC 5.2
    with `-O3 -mavx` flags, included here for comparison purposes.

-   **Greedy search** (74.759 XORs/block): finds the longest common
    sub-sequence, extracts it and removes it from existing sequences, then
    repeats until there are no more common sub-sequences of length \> 1. This is
    a fairly exhaustive search and very slow to perform. It is also likely
    impractical (ignoring JIT speed), since register allocation isn’t
    considered, and there is likely not enough to handle all the temporary
    sequences. Lastly, it completely ignores achievable parallelism (and hence,
    likely not good)

-   **Greedy cost search** (72.608 XORs/block): as above, but finds the best
    common sub-sequence (amongst pairs) which minimises total XORs at each step
    of the reduction. This search also takes into consideration the
    invertability of XOR (e.g. `(a=b^c) == (a=b^c^d^d)`). Search is slower to
    perform than above, but finds slightly better matches.  
    As above, impractical to implement, but can be an indication of a lower
    limit of what’s theoretically possible.

Considering the difference between the greedy searches and the implementable
heuristics, the figures suggest that there may be fair gains to be had with more
efficient algorithms.

Note that the average figures above may be incorrect as I haven’t checked them
thoroughly.

### Note on Parallelism

CPUs can execute multiple instructions per clock if there’s no dependency chain
between them. Unfortunately, many optimal XOR sequences may actually perform
worse than a “suboptimal” number of XORs because of this.

Taking the example from above:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
w = a^b^c
x = w^d
y = x^a
z = x^c
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*x* cannot be calculated until *w* has been, and *y* and *z* cannot be
calculated until *x* has been. However, once *x* has been calculated, *y* and
*z* can be calculated in parallel. This means that if one XOR takes one clock
cycle, the above sequence requires 4 cycles to compute on a superscalar
processor (assuming no other overheads).

On the other hand, consider the following equivalent sequence:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
temp = b^d
w = a^b
y = temp^c
w = w^c
z = temp^a
x = w^d
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Although there are 6 XORs, *temp* and *w* can be calculated in parallel, as well
as the other lines. This means that the sequence can actually be computed in 3
cycles, and hence faster than the “optimal” sequence despite needing one more
XOR operation.

AVX
---

We use 128 bit operations only because it’s the optimal size for SSE
instructions. The algorithm can trivially be adopted for any number of bits
(including non-SIMD sizes) and only needs load, store and XOR operations to
function.

Using 256 bit AVX operations improves performance over 128 bit SSE operations,
plus the 3 operand syntax of AVX is useful for eliminating MOV instructions if
optimal sequences (above) are to be used. AVX512’s 32 registers may also give a
very minor boost by allowing all 16 inputs to be cached (which also eliminates
the need to handle cases when the source and destination buffers are the same).
The ternary logic instruction, which effectively can perform 2 XORs at once,
further improves speed.

The downside is that 256 bit operations would require processing to be performed
on rather large 512 byte blocks. Also, the JIT operation is relatively slow,
which becomes a larger portion of overall time when the main processing loop
becomes faster.

I haven’t yet explored using an AVX(1) implementation. Initial tests with AVX
(floating point operations) seem to show minimal benefit\* on Intel CPUs due to
limited FP concurrency. I expect AMD Bulldozer/Jaguar based CPUs not to yield
any benefit when multi-threading when AVX operations are used. Hence, it seems
like AVX2 capable CPUs are necessary for better performance.

\* it may be possible to mix 128 bit integer and 256 bit FP operations to
improve throughput, but, despite the complexity, would only matter to Intel
Sandy/Ivy Bridge CPUs

ParPar includes a AVX2 and AVX512BW implementation of the algorithm. Despite
limitations, these seem to perform fairly well, however the implementations have
basically needed to be rewritten for each vector size to take advantage of the
features provided by the AVX extension. Hence, these implementations are quite
different from the SSE2 implementation, and have little code in common.

x86 Micro-optimisations (JIT)
-----------------------------

-   To minimise the effects of L1 cache latency, preload as many inputs as can
    fit in registers. Since the algorithm needs 3 registers to operate, 13 of
    the 16 inputs can be preloaded, whilst the remaining 3 need to be
    referenced. For 32-bit mode, where only 8 XMM registers are available, we
    can only preload 5 inputs.

-   Both `XORPS` and `PXOR` instructions can be used for XOR operations. `XORPS`
    has the advantage of being 1 byte shorter than `PXOR`, however doesn't seem
    be as performant (limited concurrency on Intel CPUs). As such, we prefer
    using `PXOR`, but mix in some `XORPS` instructions, which seems to improve
    overall performance slightly.  
    Note that `XORPD` is a largely useless instruction, and not considered here.
    Also, there is no length difference for (E)VEX encoded instructions, so we
    always prefer `VPXOR` for AVX code.

-   For AVX512 implementation, the `VPTERNLOG` instruction can replace two XOR
    operations, and this one instruction is just as fast as a `VPXOR`. If the
    number of XORs isn’t even, we can start a chain with a `VMOVDQA` (which
    effectively gets move eliminated) which makes the remaining number of XORs
    even. Testing on a Skylake-X processor, however, shows that the AVX512
    implementation seems to just about saturate L2 cache bandwidth, so this
    optimisation may not have as large of a visible effect.

-   Memory offsets between -128 and 127 can be encoded in 1 byte, otherwise 4
    byte offsets are necessary. Since we operate on 256 byte blocks, we can
    exploit this by pre-incrementing pointers by 128. For example, instead of
    referring to:  
    `out[0], out[16], out[32] ... out[224], out[240]`  
    we can +128 to the 'out' pointer and write:  
    `out[-128], out[-112], out[-96] ... out[96], out[112]`  
    This trick allows all the memory offsets to be encoded in 1 byte, which can
    help if instruction fetch is a bottleneck.

    -   It follows that larger block sizes (i.e. AVX2 implementation) will cause
        some instructions to exceed this one byte range, but offsetting is still
        beneficial since -256 to 255 has fewer long instructions than 0 to 511.
        AVX512’s EVEX coding scheme supports compressed displacement addressing,
        which makes this a moot point.

-   The JIT routine uses a number of speed tricks, which may make the code
    difficult to read.

    -   For the SSE2 implementation, the main loop is almost entirely
        branchless, relying on some tricks, such as incrementing the write
        pointer based on a flag, and using an XOR to conditionally transform a
        `PXOR`/`XORPS`into a `MOVDQA`/`MOVAPS`instruction.

    -   For the AVX2 implementation, almost all instructions are 4 bytes in
        length, which means the algorithm can use precomputed index
        lookup-tables, expand the indicies to 32 bit and mix instruction opcodes
        in.

    -   For the AVX512 implementation, indicies can be obtained via the
        `VCOMPRESSD` instruction instead of a lookup (unsure if faster though),
        and instruction sequences can be built up in 512 bit registers via some
        shuffling and mixing of data.

Other Ideas
-----------

### Static Pre-generation

An alternative to JIT may be to statically generate kernels for every possible
multiplier. Not only does this avoid issues of JIT (memory protection and slow
code generation), it enables more exhaustive searches for optimal sequences and
further code optimisation techniques, and also makes it easier to generate code
for other platforms (e.g. ARM NEON) as we can rely on compilers for support
without writing our own JIT routines. This should be doable for small GF field
sizes, such as GF(256), however, for GF(65536), whilst it is somewhat feasible
to generate 65536 different routines, assuming we only need to the consider the
default polynomial, the resulting code size would be rather large.

From some initial testing, it seems like GCC does a fairly good job of
optimising the code (significantly better than my JIT routine), so I’ve just
given it the raw sequences without trying to do any deduplication. On x86-64,
for w=16 polynomial 0x1100B, with xor=1 (i.e. adding the results), I get a
resulting \~60MB binary, for a dispatcher and all 65534 kernels (note that
GF-Complete handles multiply by 0 and 1 for us).

Unfortunately the performance from this is very poor. Whilst fetching code from
memory (60MB is guaranteed to cache miss) should still be much faster than JIT,
I suspect that the overhead of the pagefault generated (TLB misses etc),
whenever a kernel is executed, to be the main cause of slowdown.

It may be worthwhile exploring the use of large memory pages and/or compacting
the code (which is dynamically unpacked with JIT) to mitigate the performance
issues. However, due to the significant drawbacks of using pre-generated code
(large code size and need for large page support (which seems a little unwieldy
in current OSes)), I have not explored these options.

### Split Multiplication

Instead of trying to pre-generate 65534 different kernels, perhaps we could
exploit the distributive property of multiplication and divide the problem into
2x 8-bit multiplies (similar to how SPLIT_TABLE(16,8) works). This reduces the
number of kernels required to 511, a much more feasible number.

Whilst this has all the advantages of static pre-generation, one serious
downside is that it doubles the amount of work required, as we are performing 2
multiplies instead of 1. In theory, it should be more than half as fast as a JIT
implementation, since we can exploit some synergies such as caching some inputs
to registers, or reducing loop overhead, but it's likely to still be
significantly slower than a JIT implementation of the full 16-bit multiply.

This may be a viable approach if JIT cannot be performed, as long as one doesn't
mind compiling 511 kernels (which is still quite a significant number). A pure C
implementation may also not be able to exploit certain optimal strategies, due
to the lack of variable goto support (GCC extensions to C allow this however).

### Multi-region Optimization

Typically we’re going to be multiplying multiple regions, not just one region,
so there may be ways to exploit this.

One idea is to JIT kernels for multiple values before actually executing them.
In theory, this may allow CPUs, which speculatively execute far ahead, more time
to deal with self-modifying code. It also may amortize the cost of kernel calls
if we wish to be W\^X friendly.

Initial testing seems to show that this strategy can have noticeable effects on
performance, though they appear to be linked with Intel i3/i5/i7 range CPUs
generating fewer SMC (machine clear due to self modifying code) events, rather
than being more friendly to speculative execution. This gain is mostly noticed
when processing small blocks of data, however, for larger blocks, this generally
performs worse, probably due to the less efficient cache usage.

 

GFNI
====

Intel announced a new instruction set with their upcoming Icelake processors
(expected around 2019), the Galois Field New Instructions (GFNI). Of particular
interest is the new `GF2P8AFFINEQB` instruction which performs an affine
transformation on 8 bit vectors. The instruction effectively allows us to
selectively XOR bits to produce output, which is exactly what we do here.

Unfortunately, the instruction only works with 8 bit words, however it can still
be used by splitting a 16 bit word into halves and processing the 8 bit
components separately. Doing so, we get something that is very similar to how
the SPLIT_TABLE(16,4) algorithm works, and in fact, such an implementation is
largely a mix of this XOR_DEPENDS algorithm and the SPLIT_TABLE(16,4) technique.
That is, we calculate bit dependencies as described above, and the main loop
operates similarly to SPLIT_TABLE(16,4). This algorithm is implemented in
ParPar, dubbed AFFINE. Note that ParPar’s implementation is using the ALTMAP
optimization, and the layout is identical to that for SPLIT_TABLE(16,4).

Performance will ultimately depend on how fast the instruction runs on the CPU,
but one `GF2P8AFFINEQB` effectively processes all bits from the input, whereas
`PSHUFB` only processes half of the input (4 bits per byte). As such, only two
`GF2P8AFFINEQB` instructions (plus a `PXOR`) is needed to process a vector of
data, compared to four `PSHUFB` instructions. So if we assume `GF2P8AFFINEQB`
has the same throughput as `PSHUFB`, this new method should be roughly double in
speed compared to SPLIT_TABLE(16,4), and hence, likely be the fastest method
available on supporting CPUs.

 

Comparison with Cauchy
======================

The technique is very similar to the Cauchy method that GF-Complete implements.
Differences include:

-   instead of partitioning the whole region into *w* sub-regions, we partition
    *b \* w* byte blocks into *w* sub-regions, where *b* = 16 bytes (SSE word
    size)

-   calculate a dependency mask *before* performing XOR-based multiplication

 

Drawbacks
=========

Compared to most other techniques, the XOR_DEPENDS algorithm has a number of
drawbacks:

-   Processing block size is relatively large at 256 bytes. SPLIT_TABLE(16,4),
    for example only needs to process using 32 byte blocks.

-   If the source (multiplicand) and destination (products) is the same area,
    the algorithm needs to retain a copy of the processing block before it can
    be written out. This is less of an issue in a 64 bit environment, as the 16
    XMM registers can hold 256 bytes, however on 32 bit platforms, a temporary
    location needs to be used.  
    If possible, supply different regions for the source and destination for
    best performance.

    -   A separate code branch is implemented to handle the case of src==dest,
        as well as seperate branches for 32 bit and 64 bit in the SSE2 JIT
        version (AVX2 version avoids this complexity by just copying the source
        to a temporary location)

    -   This separate branch does not implement the heuristic optimisation to
        remove some unnecessary XORs (complications with managing register usage
        and additional temporary variables), so expect a speed penalty from that

    -   This isn’t an issue with the (64-bit) AVX512 implementation as there are
        sufficient registers available to not require a temporary memory store

-   JIT requires memory to be allocated with write and execute permissions

-   Overhead of JIT is comparitively high; JIT works best when fed larger
    buffers

-   Single use JIT can usually trigger self-modifying code behaviour on
    processors, which increases the overhead of this technique

-   As with other ALTMAP implementations, the data needs to be re-arranged
    before and after processing. Unfortunately, the process of converting
    to/from this arrangement isn't particularly fast (processes 16 bits at a
    time), so an implementation which doesn't require alternative mappings (i.e.
    does the re-arrange automatically per block) is unlikely to be practical.

-   Some of the speed is reliant on availability of XMM registers. Basically
    this means that performance on 32-bit won’t be good as 64-bit due to less
    registers being available.

-   Whilst the idea is simple, an optimised implementation is just very
    complicated. I really like the SPLIT(16,4) algorithm for its aesthetic
    simplicity and efficiency.

 

Usage in ParPar
===============

GF-Complete uses SPLIT_TABLE(16,4) method by default, if SSSE3 is available.
ParPar also enables the ALTMAP optimisation (rearrange input and output) for
better performance, and extends the SSSE3 implementation to AVX2 and AVX512BW if
available.

If SSSE3 is unavailable, GF-Complete falls back to SPLIT_TABLE(16,8), which is
roughly 5 times slower than SPLIT(16,4) with ALTMAP. ParPar, instead, will fall
back to XOR_DEPENDS with alternative mapping, if SSE2 is available (or CPU is
Intel Atom/Conroe). If a memory region with read, write and execute permissions
can be mapped, the JIT version is used, otherwise the static code version is
used, which is faster than or as fast as SPLIT_TABLE(16,8).

As the current implementation of XOR_DEPENDS seems to be faster than
SPLIT_TABLE(16,4) in a number of cases, I may decide to favour it in ParPar
more.

 

Benchmarks
==========

Non-scientific benchmark using a modified version of the *time_tool.sh* (to
generate CSV output and try 4KB - 16MB blocks) provided with GF-Complete. Tests
were ran with `sh time_tool.sh R 16 {method}` for "interesting" methods. This
was repeated 3 times and the highest values (maximums) are shown here, for a few
different CPUs.

All tests were ran on Linux amd64. Results from MSYS/Windows appear to be
inaccurate (have not investigated why) and i386 builds are likely [unfairly
penalised](http://jerasure.org/jerasure/gf-complete/issues/7) as GF-Complete
appears to be designed for amd64 platforms (and it [doesn’t build without
changes](http://lab.jerasure.org/jerasure/gf-complete/issues/6)).

Notes:

-   ALTMAP is implied for XOR_DEPENDS since the data is arranged differently.
    ALTMAP implementations require conversion to/from the normal layout, the
    overhead of which is not shown in these benchmarks (for PAR2 purposes, the
    overhead is likely negligible if large number of recovery slices are being
    generated).

-   the benchmark uses different memory locations for source and destination,
    and hence, does not incur the XOR_DEPENDS speed penalty where source ==
    destination.

-   XOR_DEPENDS (JIT) does not perform as well on 32 bit builds as it does here,
    due to fewer XMM registers being available for caching

-   some processor specific optimisations are used in XOR_DEPENDS JIT code.
    These optimisations predominantly affect smaller region sizes:

    -   on Intel Core 2 (as well as first gen Atom), 128-bit unaligned stores
        are done via 64-bit instructions as they are faster than a `MOVDQU`
        instruction

    -   on Intel Nehalem and newer (Core iX processors), JIT’d code is written
        to a temporary location then copied across to the destination. This
        reduces “machine clear” events due to SMC triggers and improves
        performance significantly

-   AVX is used if supported by the CPU (GF-Complete does this by default)

-   a few optimisations have been applied to the SPLIT_TABLE(16,4)
    implementation:

    -   LUT generation uses SSE and no longer requires log table lookups

    -   LUT tables no longer stored in array, as some compilers don’t cache
        array entries in registers

    -   when XORing into the destination, this operation is performed halfway
        through the shuffles to minimise the L1 cache latency

Intel Core 2 (65nm)
-------------------

-   CPU: Intel Pentium Dual Core T2310 @1.46GHz

    -   32KB L1 cache, 1MB shared L2 cache, SSE2 and SSSE3 supported

-   RAM: 2GB DDR2 533MHz

-   Compiler: GCC 4.8.4

![](CoreT2310.png)

**Comment**: The Conroe CPU has a [relatively
slow](http://forum.doom9.org/showthread.php?p=1668136#post1668136) `pshufb`
instruction, hence XOR_DEPENDS usually comes out on top  

 

Intel Silvermont
----------------

-   CPU: Intel Atom C2350 @2.0GHz

    -   24KB L1 cache, 1MB shared L2 cache, SSE2 and SSSE3 supported

-   RAM: 4GB DDR3 1600MHz

-   Compiler: GCC 4.9.2

![](AtomC2350.png)

**Comment**: It seems like SPLIT_TABLE(16,4) is *really* slow on the Atom, only
slightly faster than SPLIT_TABLE(16,8), probably due to the `pshufb` instruction
taking [5 cycles to execute on
Silvermont](http://www.agner.org/optimize/instruction_tables.pdf)

 

AMD K10
-------

-   CPU: AMD Phenom 9950 @2.6GHz

    -   64KB L1 cache, 512KB L2 cache, SSE2 supported

-   RAM: 4GB DDR2 800MHz dual channel

-   Compiler: GCC 4.4.3

![](Phenom9950.png)

**Comment**: SPLIT_TABLE(16,4) is not using SIMD here, as the CPU does not
support SSSE3. As expected, XOR_DEPENDS is faster than SPLIT_TABLE(16,8)
although the non-JIT version is about the same.

 

Intel Sandy Bridge
------------------

-   CPU: Intel Core i5 2400 @3.1GHz

    -   32KB L1 cache, 256KB L2 cache, 6MB shared L3 cache, SSE2, SSSE3 and AVX
        supported

-   RAM: 4GB DDR3 1333MHz dual channel

-   Compiler: GCC 5.2.1

![](CoreI2400.png)

**Comment**: XOR_DEPENDS can actually beat SPLIT_TABLE(16,4) here, despite Sandy
Bridge having a fast shuffle unit, and the SPLIT method taking advantage of AVX.
At smaller buffer sizes, the high overhead of JIT rears its head though.

 

Multi-Threaded Benchmarks
-------------------------

A small, rough alteration to the *gf_time* tool to run tests in 4 threads.
Region data is no longer randomised to try to keep cores maximally loaded with
calculations we wish to benchmark.

![](Phenom9950mt.png)

![](CoreI2400mt.png)

 

Patch
=====

**This patch is now out of date as I can’t be bothered maintaining it; the
latest implementation can be found in ParPar. If anyone is interested in such a
patch, raise a Github issue.**

A rather hacky patch for GF-Complete, which should apply cleanly to the v2 tag,
can be [found here](gfc.patch).

The patch only implements XOR_DEPENDS for w=16. Since ALTMAP is implied and
always used, I abuse the `-r ALTMAP` flag to turn on JIT (i.e. use `-m
XOR_DEPENDS -r ALTMAP -` to use the JIT version, and just `-m XOR_DEPENDS -` for
non-JIT version).

The patch does also include an implementation for `extract_word` so that it's
compatible with *gf_unit*.

Other things I included because I found useful, but you mightn’t:

-   gf_unit failures also dump a 256 source/target memory region; fairly crudely
    implemented but may assist debugging

-   crude *time_tool.sh* modifications for CSV output used in benchmarks above

-   intentionally breaking code for w=32,64,128 to enable compilation on i386
    platforms (basically instances of `_mm_insert_epi64` and `_mm_extract_epi64`
    have been deleted with no appropriate replacement)

-   slightly improved speed of LUT generation for SPLIT_TABLE(16,4) SSSE3
    versions by removing duplicate log table lookups
