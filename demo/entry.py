import argparse
import os
import subprocess
import torch
import csv
from model import DLRegAlloc
from utils import process_model_input, process_model_output


def run_X86IGGenerator(c_file):
    """
    Generate interference graph and rename the file w.r.t. c_file name.
    The content in the interference graph follows the format below:
        #, 200 long, 100 #
    """
    subprocess.run(["sh", "iggenerator.sh", c_file])
    os.rename("interference.csv", c_file + "_ig.csv")

def run_DL_model(ig_file):
    device = "cuda" if torch.cuda.is_available() else "cpu"
    print("Using", device, "...\n")
    loaded_model = DLRegAlloc().to(device)
    loaded_model.load_state_dict(torch.load(f="dl_regalloc_model.pth"))
    model_input = process_model_input(ig_file, device) # shape(1, 100, 100)
    model_output = process_model_output(model_input, loaded_model)
    model_output = model_output[0]
    return model_output

def run_regalloc_pass(c_file):
    subprocess.run(["sh", "regalloc.sh", c_file])

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-f', '--file', default=None, type=str)
    args = parser.parse_args()

    print("Demo starting...\n")

    # read in c file
    c_file = args.file
    if c_file is None:
        c_file = input("Provide c file name: ")
        print()
    c_file = c_file.split(".")[0]
    
    # run X86IGGenerator pass on the c_file
    run_X86IGGenerator(c_file)
    print()

    # run deep learning model
    model_output = run_DL_model(c_file + "_ig.csv")
    # run_DL_model("baidu.csv")

    # write to csv
    first_flag = True
    with open('model_output.csv', 'w') as output_file:
        for color in model_output:
            if first_flag:
                output_file.write(str(color))
                first_flag = False
            else:
                output_file.write(", " + str(color))
    
    print()
    print()
    print("Run RegAlloc Pass...")

    run_regalloc_pass(c_file)


if __name__ == "__main__":
    main()

