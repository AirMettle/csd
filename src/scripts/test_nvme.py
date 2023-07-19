import os
import pandas as pd
import sys, subprocess
import re

BDEVNAME = os.getenv("KVCLI_BDEVNAME", "Nvme1n1")
EXE_PATH = os.getenv("KVCLI_EXE_PATH", "./build/examples/kvcli")
TEST_DIR = os.getenv("KVCLI_TEST_DIR", "./tests")
TMP_DIR = os.getenv("KVCLI_TMP_DIR", "./tmp")

num_errors = 0
num_success = 0

def save_to_nvme(path):
    key = os.path.basename(path)
    subprocess.run([EXE_PATH, BDEVNAME, "store", "--key", key, "--file", path], capture_output=True)

def query_nvme(key, query, data_type, output_path):
    subprocess.run([EXE_PATH, BDEVNAME, "select", "--key", key, "--sql", query, "--input_format", data_type.lower(), "--output_format", data_type.lower(), "--file", output_path, "--use_csv_header_for_input", "--use_csv_header_for_output"], capture_output=True)

def read_from_nvme(key, output_path):
    subprocess.run([EXE_PATH, BDEVNAME, "retrieve", "--key", key, "--file", output_path], capture_output=True)

def convert_to_parquet(path, output_path):
    df = pd.read_csv(path)
    df.to_parquet(output_path, engine='pyarrow')

def list_files_on_nvme(key):
    result = subprocess.run([EXE_PATH, BDEVNAME, "list", "--key", key if key else ""], capture_output=True, text=True)
    out = result.stderr
    pattern = r'key\[\d+\]\s=\s(\S+)'
    matches = re.findall(pattern, out)
    return matches

def files_equal(path1, path2):
    with open(path1, 'rb') as f1, open(path2, 'rb') as f2:
        return f1.read() == f2.read()

def read_file(path):
    try:
        with open(path, 'r') as f:
            return f.read()
    except FileNotFoundError:
        return None

def delete_file_from_nvme(key):
    subprocess.run([EXE_PATH, BDEVNAME, "delete", "--key", key], capture_output=True)
    
def test_existance_on_nvme(key):
    result = subprocess.run([EXE_PATH, BDEVNAME, "exists", "--key", key], capture_output=True, text=True)
    out = result.stderr
    if 'Key exists.' in out:
        return True
    elif 'Key does not exist.' in out:
        return False
    return None

def log_error(str):
    global num_errors
    print(str)
    num_errors += 1

def log_success(str):
    global num_success
    print(str)
    num_success += 1

def process_files(file_directory, tmp_directory):
    os.makedirs(tmp_directory, exist_ok=True)
    files = os.listdir(file_directory)
    csv_files = [f for f in files if f.endswith('.csv')]
    uploaded_files = []
    for csv_file in csv_files:
        if len(csv_file) > 16:
            print(f"length of file name {csv_file} is over 16")
            sys.exit(-1)

        # Upload file
        csv_path = os.path.join(file_directory, csv_file)
        save_to_nvme(csv_path)
        uploaded_files.append(csv_file)

        # Download file and make sure contents are correct
        tmp_path = f"{tmp_directory}/{csv_file}"
        read_from_nvme(csv_file, tmp_path)
        if not files_equal(csv_path, tmp_path):
            log_error(f"ERROR: read data for {csv_file} does not match")
        else:
            log_success(f"SUCCESS: read data for {csv_file} matches")
      
        os.remove(tmp_path)

        # Files consists of a csv file (e.g. test.csv), files with queries to run against it (e.g. test.query1, test.query2)
        # and expected results from the query (e.g. test.result1, test.result2)
        query_num = 1
        while True:
            query_path = f"{file_directory}/{csv_file.replace('.csv', '')}.query{query_num}"
            result_path = f"{file_directory}/{csv_file.replace('.csv', '')}.result{query_num}"
            query_data = read_file(query_path)
            if not query_data:
                if query_num == 1:
                    print(f"No queries found for {csv_file}")
                break
            if not os.path.isfile(result_path):
                print(f"No result file for {csv_file} query {query_num}")
                query_num += 1
                continue
            
            # Run query and make sure contents are correct
            tmp_path = f"{tmp_directory}/{csv_file}"
            query_nvme(csv_file, query_data, "CSV", tmp_path)
            if not files_equal(result_path, tmp_path):
                log_error(f"ERROR: query csv data for {csv_file} query {query_num} does not match")
            else:
                log_success(f"SUCCESS: query csv data for {csv_file} query {query_num} matches")
            os.remove(tmp_path)
            
            query_num += 1
            
    parquet_files = [f for f in files if f.endswith('.parquet')]
    for parquet_file in parquet_files:
        if len(parquet_file) > 16:
            print(f"length of file name {parquet_file} is over 16")
            sys.exit(-1)

        # Upload file
        parquet_path = os.path.join(file_directory, parquet_file)
        save_to_nvme(parquet_path)
        uploaded_files.append(parquet_file)

        # Download file and make sure contents are correct
        tmp_path = f"{tmp_directory}/{parquet_file}"
        read_from_nvme(parquet_file, tmp_path)
        if not files_equal(parquet_path, tmp_path):
            log_error(f"ERROR: read data for {parquet_file} does not match")
        else:
            log_success(f"SUCCESS: read data for {parquet_file} matches")
        os.remove(tmp_path)

        # Files consists of a csv file (e.g. test.csv), files with queries to run against it (e.g. test.query1, test.query2)
        # and expected results from the query (e.g. test.result1, test.result2)
        query_num = 1
        while True:
            query_path = f"{file_directory}/{parquet_file.replace('.parquet', '')}.query{query_num}"
            result_path = f"{file_directory}/{parquet_file.replace('.parquet', '')}.result{query_num}"
            query_data = read_file(query_path)
            if not query_data:
                if query_num == 1:
                    print(f"No queries found for {parquet_file}")
                break
            if not os.path.isfile(result_path):
                print(f"No result file for {parquet_file} query {query_num}")
                query_num += 1
                continue
            
            # Run query and make sure contents are correct
            tmp_path = f"{tmp_directory}/{parquet_file}"
            query_nvme(parquet_file, query_data, "parquet", tmp_path)
            if not files_equal(result_path, tmp_path):
                log_error(f"ERROR: query parquet data for {parquet_file} query {query_num} does not match")
            else:
                log_success(f"SUCCESS: query parquet data for {parquet_file} query {query_num} matches")
            os.remove(tmp_path)

            query_num += 1

    # Check that list files is correct
    list_files = list_files_on_nvme(None)
    if not all(elem in list_files for elem in uploaded_files):
        log_error("ERROR: List files does not match")
    else:
        log_success("SUCCESS: List files matches")
        
    # Test existance of uploaded files
    for f in uploaded_files:
        if not test_existance_on_nvme(f):
            log_error(f"ERROR: Existence test fails for {f}")
        else:
            log_success(f"SUCCESS: Existence test passes for {f}")
    
    # Delete all files
    for d in uploaded_files:
        delete_file_from_nvme(d)

    # Check that list files is correct
    list_files = list_files_on_nvme(None)
    if not all(element not in list_files for element in uploaded_files):
        log_error("ERROR: Files were not deleted")
    else:
        log_success("SUCCESS: Files were deleted")
    print(f"\n\nNum Errors: {num_errors}\nNum Success: {num_success}")

process_files(TEST_DIR, TMP_DIR)
