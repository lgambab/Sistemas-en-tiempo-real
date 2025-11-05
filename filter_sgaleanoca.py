def commit_callback(commit, metadata):
    author = commit.author_name.decode('utf-8', errors='ignore').strip()
    email = commit.author_email.decode('utf-8', errors='ignore').strip()
    if author == "sgaleanoca" or email == "sgaleanoca@unal.edu.co":
        commit.skip()
