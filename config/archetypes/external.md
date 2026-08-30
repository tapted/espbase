+++
# date = '{{ .Date }}'
date: '{{ now.Format "2006-01-02" }}'
draft = false
title = '{{ replace .File.ContentBaseName "-" " " | title }}'
+++
