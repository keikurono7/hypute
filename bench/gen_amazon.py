"""
Streams Amazon Reviews '23 rating-only files from UCSD and writes a numeric CSV
compatible with the benchmark_runner (user_id, item_id, rating, timestamp).
"""
import csv
import gzip
import hashlib
import io
import sys
import urllib.request

output_csv    = sys.argv[1]
target_records = int(sys.argv[2]) if len(sys.argv) > 2 else 100_000_000

SOURCES = [
    ("Books",                      "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Books.csv.gz"),
    ("Home_and_Kitchen",           "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Home_and_Kitchen.csv.gz"),
    ("Electronics",                "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Electronics.csv.gz"),
    ("Clothing_Shoes_and_Jewelry", "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Clothing_Shoes_and_Jewelry.csv.gz"),
    ("Health_and_Household",       "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Health_and_Household.csv.gz"),
    ("Beauty_and_Personal_Care",   "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Beauty_and_Personal_Care.csv.gz"),
    ("Cell_Phones_and_Accessories","https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Cell_Phones_and_Accessories.csv.gz"),
    ("Automotive",                 "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Automotive.csv.gz"),
    ("Sports_and_Outdoors",        "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Sports_and_Outdoors.csv.gz"),
    ("Tools_and_Home_Improvement", "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Tools_and_Home_Improvement.csv.gz"),
    ("Pet_Supplies",               "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Pet_Supplies.csv.gz"),
    ("Patio_Lawn_and_Garden",      "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Patio_Lawn_and_Garden.csv.gz"),
    ("Toys_and_Games",             "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Toys_and_Games.csv.gz"),
    ("Grocery_and_Gourmet_Food",   "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Grocery_and_Gourmet_Food.csv.gz"),
    ("Unknown",                    "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/benchmark/0core/rating_only/Unknown.csv.gz"),
]


def stable_u64(value: str) -> int:
    digest = hashlib.blake2b(value.encode(), digest_size=8).digest()
    return int.from_bytes(digest, "big", signed=False)


written = 0
with open(output_csv, "w", newline="") as fh:
    writer = csv.writer(fh)
    writer.writerow(["user_id", "item_id", "rating", "timestamp"])

    for category, url in SOURCES:
        print(f"[HAYAKO] Streaming {category}...", flush=True)
        try:
            with urllib.request.urlopen(url, timeout=120) as resp:
                with gzip.GzipFile(fileobj=resp) as gz:
                    reader = csv.DictReader(io.TextIOWrapper(gz, encoding="utf-8"))
                    for row in reader:
                        uid = row.get("user_id")
                        iid = row.get("parent_asin")
                        rat = row.get("rating")
                        ts  = row.get("timestamp")
                        if not uid or not iid or rat is None or ts is None:
                            continue
                        writer.writerow([
                            stable_u64(uid),
                            stable_u64(iid),
                            float(rat),
                            int(float(ts)),
                        ])
                        written += 1
                        if written % 1_000_000 == 0:
                            print(f"[HAYAKO] {written:,} rows written...", flush=True)
                        if written >= target_records:
                            break
        except Exception as exc:
            print(f"[HAYAKO] Skipping {category}: {exc}", flush=True)

        if written >= target_records:
            break

if written == 0:
    sys.exit("[HAYAKO] No rows converted — check network access to mcauleylab.ucsd.edu")

print(f"[HAYAKO] Done — {written:,} rows written to {output_csv}", flush=True)
