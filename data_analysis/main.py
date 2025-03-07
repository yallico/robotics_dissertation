import boto3

BUCKET_NAME = "robotics-dissertation"
PREFIX = "DA8C/" 
REGION = "eu-north-1" 

# init
s3 = boto3.client("s3", region_name=REGION, config=boto3.session.Config(signature_version="s3v4"))

# list
response = s3.list_objects_v2(Bucket=BUCKET_NAME, Prefix=PREFIX)

# test
if "Contents" in response:
    for obj in response["Contents"]:
        print(obj["Key"])
else:
    print("No files found in the bucket.")
