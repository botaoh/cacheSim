#!/bin/bash

# Define trace files
TRACE_DIR="short_traces"
TRACES=("short_gcc.trace" "short_leela.trace" "short_linpack.trace" "short_matmul_naive.trace" "short_matmul_tiled.trace" "short_mcf.trace")

# Output log file
OUTPUT_LOG="test_results.log"

# Clear previous log
echo "Starting cache experiments..." > $OUTPUT_LOG

# Loop through all trace files
for TRACE in "${TRACES[@]}"; do
    echo "Testing with trace: $TRACE" | tee -a $OUTPUT_LOG

    # 1. L1 Cache Sensitivity Test (L1 Associativity ≤ L2 Associativity)
    for C1 in 14 15 16; do  # L1 size (16KB, 32KB, 64KB)
        for B1 in 5 6; do  # Block size (32B, 64B)
            for S1 in 2 4; do  # L1 Associativity (4-way, 8-way)
                for S2 in 4 8; do  # L2 Associativity (Ensure L1 S1 ≤ L2 S2)
                    if [[ $S1 -le $S2 ]]; then
                        CMD="./cachesim -c $C1 -b $B1 -s $S1 -C 17 -S $S2 -P LIP < $TRACE_DIR/$TRACE"
                        echo $CMD | tee -a $OUTPUT_LOG
                        eval $CMD >> $OUTPUT_LOG
                    fi
                done
            done
        done
    done

    # 2. L2 Cache Impact Test (Enable vs Disable)
    for C2 in 17 18 19; do  # L2 sizes (128KB, 256KB, 512KB)
        for S2 in 4 8; do  # L2 Associativity (Ensure L1 S1 ≤ L2 S2)
            for S1 in 2 4; do
                if [[ $S1 -le $S2 ]]; then
                    CMD="./cachesim -c 15 -b 6 -s $S1 -C $C2 -S $S2 -P LIP < $TRACE_DIR/$TRACE"
                    echo $CMD | tee -a $OUTPUT_LOG
                    eval $CMD >> $OUTPUT_LOG
                fi
            done
        done
    done

    # 3. L2 Replacement Policy Comparison
    for POLICY in MIP LIP FIFO RANDOM; do
        CMD="./cachesim -c 15 -b 6 -s 2 -C 18 -S 4 -P $POLICY < $TRACE_DIR/$TRACE"
        echo $CMD | tee -a $OUTPUT_LOG
        eval $CMD >> $OUTPUT_LOG
    done

    # 4. Early Restart Evaluation
    CMD="./cachesim -c 15 -b 6 -s 2 -C 18 -S 4 -P LIP -E < $TRACE_DIR/$TRACE"
    echo $CMD | tee -a $OUTPUT_LOG
    eval $CMD >> $OUTPUT_LOG

    # 5. Victim Cache Impact
    for VC in 0 1 2; do  # Victim cache entries (0, 1, 2)
        CMD="./cachesim -c 15 -b 6 -s 2 -C 18 -S 4 -P LIP -v $VC < $TRACE_DIR/$TRACE"
        echo $CMD | tee -a $OUTPUT_LOG
        eval $CMD >> $OUTPUT_LOG
    done

done

echo "All tests completed!" | tee -a $OUTPUT_LOG
